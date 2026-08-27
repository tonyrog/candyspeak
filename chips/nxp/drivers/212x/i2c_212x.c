// LPC2000 I2C master, polled. See i2c_212x.h for why polled.
//
// The whole driver is the master half of the state machine in the user manual,
// driven by a loop instead of an interrupt. Every step is the same three moves:
// put something in DAT or CON, release SI, wait for SI to come back, and check
// STAT. A step that returns an unexpected status stops the transfer and leaves
// the bus with a STOP on it -- never half-open, because a master that walks away
// mid-transfer holds SDA low and takes the bus down for everyone.

#include <stdint.h>
#include "csp_config.h"
#include "chip_212x.h"
#include "i2c_212x.h"

// How long to wait for the hardware to answer, in loop iterations. At 60 MHz
// this is a few milliseconds -- far longer than any byte at 100 kHz (90 us) and
// short enough that a stuck bus does not become a dead board.
//
// BOUNDED, for the same reason every wait in Chip_SystemInit is: an unbounded
// spin turns a pulled-out cable into a part that does nothing at all, forever,
// with no way to tell that from a dozen other faults.
#ifndef I2C_SPIN
#define I2C_SPIN 200000u
#endif

static int wait_si(LPC_I2C_T *i2c)
{
    uint32_t n;

    for (n = I2C_SPIN; n; n--)
	if (i2c->CONSET & I2C_CON_SI)
	    return 0;
    return -1;
}

// Release the state machine and wait for the next stop-and-ask.
static int step(LPC_I2C_T *i2c)
{
    i2c->CONCLR = I2C_CON_SI;
    return wait_si(i2c);
}

// End the transfer. Called on every path out, success or not.
//
// The SI clear must come AFTER STO is set: the stop condition is generated when
// the state machine is released, so releasing first sends whatever was pending
// and only then stops.
static void i2c_stop(LPC_I2C_T *i2c)
{
    uint32_t n;

    i2c->CONSET = I2C_CON_STO;
    i2c->CONCLR = I2C_CON_SI;
    // STO is cleared by the hardware once the stop condition is on the wire.
    // Waiting for that means the next START cannot be issued into a bus that is
    // still finishing the previous transfer.
    for (n = I2C_SPIN; n; n--)
	if (!(i2c->CONSET & I2C_CON_STO))
	    return;
}

// START (or repeated START) followed by the address byte.
// `read` picks the direction bit and therefore which status codes are right.
static int addr_phase(LPC_I2C_T *i2c, uint8_t sla, int read, int repeated)
{
    i2c->CONSET = I2C_CON_STA;
    if (repeated) {
	// A repeated start is issued by releasing the machine with STA set --
	// the first start got here by CONSET alone, because the machine was
	// idle and had no SI to release.
	if (step(i2c) < 0)
	    return -1;
    }
    else if (wait_si(i2c) < 0)
	return -1;

    {
	uint8_t st = i2c->STAT;
	if ((st != I2C_ST_START) && (st != I2C_ST_RSTART))
	    return -1;
    }

    i2c->DAT = (uint8_t)(read ? (sla | 1u) : (sla & 0xfeu));
    i2c->CONCLR = I2C_CON_STA;          // or the next release starts again
    if (step(i2c) < 0)
	return -1;

    // A NACK here is the normal answer from an absent device -- and from an
    // EEPROM in the middle of its write cycle, which is what makes ACK polling
    // work. The caller decides whether that is an error.
    return (i2c->STAT == (read ? I2C_ST_SLAR_ACK : I2C_ST_SLAW_ACK)) ? 0 : -1;
}

static int write_bytes(LPC_I2C_T *i2c, const uint8_t *b, uint16_t n)
{
    uint16_t i;

    for (i = 0; i < n; i++) {
	i2c->DAT = b[i];
	if (step(i2c) < 0)
	    return -1;
	if (i2c->STAT != I2C_ST_DATW_ACK)
	    return -1;
    }
    return 0;
}

void Chip_I2C_Init(LPC_I2C_T *i2c)
{
    // Everything off first. Arriving here with the interface already enabled --
    // a warm reset, a second /load -- and setting STA on top of it issues a
    // start into a transfer that was never finished.
    i2c->CONCLR = I2C_CON_AA | I2C_CON_SI | I2C_CON_STA | I2C_CON_EN;
    i2c->ADR = 0;                       // master only: we answer to nothing
    Chip_I2C_SetClockRate(i2c, 100000u);
    i2c->CONSET = I2C_CON_EN;
}

void Chip_I2C_SetClockRate(LPC_I2C_T *i2c, uint32_t hz)
{
    uint32_t div = Chip_Clock_GetPeripheralClockRate() / (hz ? hz : 100000u);
    uint32_t lo  = div / 2u;
    uint32_t hi  = div - lo;

    // Four is the hardware minimum for each half-period. Below it the part
    // clocks nothing and the symptom is a bus that never leaves the start
    // condition.
    if (lo < 4u) lo = 4u;
    if (hi < 4u) hi = 4u;
    if (lo > 0xffffu) lo = 0xffffu;
    if (hi > 0xffffu) hi = 0xffffu;
    i2c->SCLL = (uint16_t)lo;
    i2c->SCLH = (uint16_t)hi;
}

int Chip_I2C_MasterWrite(LPC_I2C_T *i2c, uint8_t sla,
			 const uint8_t *hdr, uint8_t hlen,
			 const uint8_t *buf, uint16_t len)
{
    int r = addr_phase(i2c, sla, 0, 0);

    if (r == 0 && hlen)
	r = write_bytes(i2c, hdr, hlen);
    if (r == 0 && len)
	r = write_bytes(i2c, buf, len);
    i2c_stop(i2c);
    return r;
}

int Chip_I2C_MasterRead(LPC_I2C_T *i2c, uint8_t sla,
			const uint8_t *hdr, uint8_t hlen,
			uint8_t *buf, uint16_t len)
{
    uint16_t i;

    // The address is WRITTEN first -- that is what sets the device's internal
    // cursor -- and then a REPEATED start turns the bus around without ever
    // releasing it. A stop in between would let another master in and leave the
    // cursor pointing wherever that master put it.
    if (addr_phase(i2c, sla, 0, 0) < 0)
	goto fail;
    if (hlen && (write_bytes(i2c, hdr, hlen) < 0))
	goto fail;
    if (addr_phase(i2c, sla, 1, 1) < 0)
	goto fail;

    for (i = 0; i < len; i++) {
	// ACK every byte except the last. The NACK on the final one is how the
	// slave is told to stop driving -- without it the device keeps clocking
	// out bytes and holds the bus through the stop.
	if ((uint16_t)(i + 1) < len)
	    i2c->CONSET = I2C_CON_AA;
	else
	    i2c->CONCLR = I2C_CON_AA;
	if (step(i2c) < 0)
	    goto fail;
	{
	    uint8_t st = i2c->STAT;
	    if ((st != I2C_ST_DATR_ACK) && (st != I2C_ST_DATR_NAK))
		goto fail;
	}
	buf[i] = i2c->DAT;
    }
    i2c_stop(i2c);
    return 0;

fail:
    i2c_stop(i2c);
    return -1;
}

int Chip_I2C_MasterProbe(LPC_I2C_T *i2c, uint8_t sla)
{
    int r = addr_phase(i2c, sla, 0, 0);
    i2c_stop(i2c);
    return r;
}
