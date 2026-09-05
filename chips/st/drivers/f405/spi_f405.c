// SPI master for the STM32F4, behind the csp_spi_* hooks.
//
// The same arrangement i2c_f405.c has and for the same reasons: the transfer
// happens inside _start and _done reports it complete. See that file's header
// for why the pair is split anyway.
//
// SPI is the simpler bus. There is no addressing -- the CHIP SELECT is the
// address -- so the endpoint carries a GPIO instead of a 7-bit number, and the
// command byte goes out before the data exactly as a register does on I2C.
// Full duplex means a read clocks out zeros and keeps what comes back.

#include <stdint.h>
#include "csp_config.h"
#include "stm32f405xx.h"
#include "csp.h"

extern const uint32_t csp_stm_pclk1;
extern const uint32_t csp_stm_pclk2;

#define SPI_SPIN 100000u

static SPI_TypeDef* spi_of(uint8_t bus)
{
    switch (bus) {
    case 1: return SPI1;
    case 2: return SPI2;
    case 3: return SPI3;
    default: return 0;
    }
}

static GPIO_TypeDef* const spi_gpio[] = {
    GPIOA, GPIOB, GPIOC, GPIOD, GPIOE, GPIOF, GPIOG, GPIOH, GPIOI
};
#define SPI_NPORT ((int)(sizeof(spi_gpio)/sizeof(spi_gpio[0])))

static void cs_set(uint8_t port, uint8_t pin, int high)
{
    if (port >= SPI_NPORT)
	return;
    // BSRR, so selecting one device cannot disturb another pin of the same
    // port that an interrupt is driving.
    spi_gpio[port]->BSRR = high ? (1u << pin) : (1u << (pin + 16u));
}

static uint16_t spi_ready;

static void spi_setup(uint8_t bus, SPI_TypeDef* d, uint8_t csport, uint8_t cspin)
{
    if (spi_ready & (1u << bus))
	return;
    // CS is an ordinary output driven by hand, not the peripheral's NSS. NSS in
    // hardware mode releases the line between bytes, which most devices read as
    // the end of the transaction -- a burst read then returns the first byte
    // over and over.
    if (csport < SPI_NPORT) {
	spi_gpio[csport]->MODER = (spi_gpio[csport]->MODER &
				   ~(3u << (cspin * 2u))) | (1u << (cspin * 2u));
	cs_set(csport, cspin, 1);      // idle high
    }
    // Mode 0, master, software NSS, and the slowest prescaler that clears
    // 1 MHz. Fast enough for any sensor and slow enough for a breadboard --
    // a board that wants more says so the day something needs it.
    d->CR1 = SPI_CR1_MSTR | SPI_CR1_SSI | SPI_CR1_SSM |
	     SPI_CR1_BR_2 | SPI_CR1_BR_1 | SPI_CR1_BR_0;   // /256
    d->CR2 = 0;
    d->CR1 |= SPI_CR1_SPE;
    spi_ready |= (1u << bus);
}

static int spi_xfer(SPI_TypeDef* d, uint8_t out, uint8_t* in)
{
    uint32_t n = SPI_SPIN;

    while (!(d->SR & SPI_SR_TXE))
	if (--n == 0) return -1;
    *(volatile uint8_t*)&d->DR = out;
    n = SPI_SPIN;
    while (!(d->SR & SPI_SR_RXNE))
	if (--n == 0) return -1;
    {
	uint8_t v = *(volatile uint8_t*)&d->DR;
	if (in) *in = v;
    }
    return 0;
}

int csp_spi_start(csp_rt_t* st, uint32_t xref, uint8_t* data, uint16_t len,
		  int is_read)
{
    uint8_t bus    = (uint8_t)TR_SPI_BUS(xref);
    uint8_t csport = (uint8_t)TR_SPI_PORT(xref);
    uint8_t cspin  = (uint8_t)TR_SPI_PIN(xref);
    uint16_t cmd   = (uint16_t)TR_SPI_CMD(xref);
    SPI_TypeDef* d = spi_of(bus);
    uint16_t i;
    int rc = 0;
    (void)st;

    if (d == 0)
	return -1;
    spi_setup(bus, d, csport, cspin);

    cs_set(csport, cspin, 0);
    // The command, then the payload. A device that wants a read bit set in the
    // command (0x80 on most of them) gets it from the DECLARATION -- `spi 1 2:4
    // 0x80` -- rather than from a rule here, because which bit and which
    // polarity is a property of the part, not of SPI.
    if (spi_xfer(d, (uint8_t)cmd, 0) < 0)
	rc = -1;
    for (i = 0; (rc == 0) && (i < len); i++) {
	if (is_read)
	    rc = spi_xfer(d, 0x00, &data[i]);
	else
	    rc = spi_xfer(d, data[i], 0);
    }
    // Wait for the shift register to empty before releasing CS: dropping it
    // while the last byte is still going out truncates that byte, and the
    // device sees a transaction one bit short.
    {
	uint32_t n = SPI_SPIN;
	while ((d->SR & SPI_SR_BSY) && --n)
	    ;
    }
    cs_set(csport, cspin, 1);
    return rc;
}

int csp_spi_done(csp_rt_t* st, uint32_t xref, uint16_t* len)
{
    (void)st; (void)xref; (void)len;
    return 1;                       // _start did the whole transfer
}
