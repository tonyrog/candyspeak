// I2C master for the STM32F4, behind the csp_i2c_* hooks.
//
// BLOCKING INSIDE _start, and 1 from _done. The hook pair exists so a transfer
// can overlap the cycle that started it, and this implementation does not use
// that -- it does the transfer and reports it complete. Nothing above notices:
// csp_buf_output starts it, csp_buf_input collects it next cycle, and the data
// is one cycle old either way.
//
// What it costs is loop time: 14 bytes at 400 kHz is about 0.35 ms against a
// 2 ms sample period, so a sensor is a sixth of a cycle. Two or three are fine.
// A dozen is not, and that is when this file grows an interrupt-driven state
// machine -- the shape above it does not have to change for that, which is the
// whole reason _start and _done are separate.
//
// V1 PERIPHERAL, and it is famously awkward: the sequences below are the ones
// the reference manual gives, in the order it gives them, including the reads
// of SR1 and SR2 that exist only to clear a flag. They look redundant and are
// not -- dropping one leaves the peripheral in a state the next transfer hangs
// in.

#include <stdint.h>
#include "csp_config.h"
#include "stm32f405xx.h"
#include "csp.h"

extern const uint32_t csp_stm_pclk1;

// Bounded waits everywhere. A missing pull-up, a held-low SDA or an absent
// device makes every one of these flags never arrive, and a bus fault must not
// take the whole node down with it -- a drone with a dead IMU still has to run
// its cutoff rules.
#define I2C_SPIN 100000u

#define I2C_WAIT(cond) do {                      \
	uint32_t _n = I2C_SPIN;                  \
	while (!(cond)) {                        \
	    if (--_n == 0) return -1;            \
	    if (i2c_error(d)) return -1;         \
	}                                        \
    } while (0)

static I2C_TypeDef* i2c_of(uint8_t bus)
{
    switch (bus) {
    case 1: return I2C1;
    case 2: return I2C2;
    case 3: return I2C3;
    default: return 0;
    }
}

static int i2c_error(I2C_TypeDef* d)
{
    if (d->SR1 & (I2C_SR1_AF | I2C_SR1_BERR | I2C_SR1_ARLO | I2C_SR1_OVR)) {
	d->SR1 &= ~(I2C_SR1_AF | I2C_SR1_BERR | I2C_SR1_ARLO | I2C_SR1_OVR);
	d->CR1 |= I2C_CR1_STOP;
	return 1;
    }
    return 0;
}

// Set up on first use rather than at board init: the board table says which
// PINS are I2C, and which BUS a program actually uses is a property of its
// declarations. A bus nothing declares stays unconfigured.
static uint16_t i2c_ready;      // bit per bus

static void i2c_setup(uint8_t bus, I2C_TypeDef* d)
{
    uint32_t mhz = csp_stm_pclk1 / 1000000u;

    if (i2c_ready & (1u << bus))
	return;
    d->CR1 = I2C_CR1_SWRST;         // a bus left stuck by a previous run comes
    d->CR1 = 0;                     // back here, and only a reset clears it
    d->CR2 = mhz;                   // the peripheral has to be told its clock
    // 400 kHz fast mode, duty 2:1. CCR = pclk / (3 * 400k) for fast mode; the
    // floor of 1 is what the manual requires and what a slow pclk would break.
    d->CCR = I2C_CCR_FS | ((csp_stm_pclk1 / (3u * 400000u)) | 1u);
    // Maximum rise time, in periods + 1. 300 ns for fast mode.
    d->TRISE = ((mhz * 300u) / 1000u) + 1u;
    d->CR1 = I2C_CR1_PE;
    i2c_ready |= (1u << bus);
}

static int i2c_start(I2C_TypeDef* d, uint8_t addr, int read)
{
    I2C_WAIT(!(d->SR2 & I2C_SR2_BUSY));
    d->CR1 |= I2C_CR1_START;
    I2C_WAIT(d->SR1 & I2C_SR1_SB);
    d->DR = (uint8_t)((addr << 1) | (read ? 1 : 0));
    I2C_WAIT(d->SR1 & I2C_SR1_ADDR);
    (void)d->SR1;                   // ADDR is cleared by reading SR1 then SR2,
    (void)d->SR2;                   // in that order. Not optional.
    return 0;
}

int csp_i2c_start(csp_rt_t* st, uint32_t xref, uint8_t* data, uint16_t len,
		  int is_read)
{
    uint8_t bus  = (uint8_t)TR_I2C_BUS(xref);
    uint8_t addr = (uint8_t)TR_I2C_ADDR(xref);
    uint8_t reg  = (uint8_t)TR_I2C_REG(xref);
    I2C_TypeDef* d = i2c_of(bus);
    uint16_t i;
    (void)st;

    if (d == 0)
	return -1;
    i2c_setup(bus, d);

    // THE REGISTER FIRST, as a write, then a REPEATED START for the read. That
    // is what "read from register 0x3B" means on this bus, and doing it as two
    // separate transactions with a stop between them lets another master --
    // or the device's own auto-increment -- move the pointer in between.
    if (i2c_start(d, addr, 0) < 0)
	return -1;
    I2C_WAIT(d->SR1 & I2C_SR1_TXE);
    d->DR = reg;
    I2C_WAIT(d->SR1 & I2C_SR1_TXE);

    if (!is_read) {
	for (i = 0; i < len; i++) {
	    d->DR = data[i];
	    I2C_WAIT(d->SR1 & I2C_SR1_TXE);
	}
	I2C_WAIT(d->SR1 & I2C_SR1_BTF);
	d->CR1 |= I2C_CR1_STOP;
	return 0;
    }

    if (i2c_start(d, addr, 1) < 0)   // repeated start, now reading
	return -1;

    // ACK every byte but the LAST, and clear ACK before reading the
    // second-to-last one. A device that gets an ACK on the final byte keeps
    // driving the bus and the next transfer starts against a held line.
    d->CR1 |= I2C_CR1_ACK;
    for (i = 0; i < len; i++) {
	if (i + 1 == len) {
	    d->CR1 &= ~I2C_CR1_ACK;
	    d->CR1 |= I2C_CR1_STOP;
	}
	I2C_WAIT(d->SR1 & I2C_SR1_RXNE);
	data[i] = (uint8_t)d->DR;
    }
    return 0;
}

int csp_i2c_done(csp_rt_t* st, uint32_t xref, uint16_t* len)
{
    (void)st; (void)xref; (void)len;
    return 1;                       // _start did the whole transfer
}
