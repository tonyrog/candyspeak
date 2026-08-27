// An I2C EEPROM behind CandySpeak's byte-stream store.
//
// The runtime asks for open/read/write/close over a running cursor -- see
// csp_eeprom.h -- and an EEPROM is exactly that, once three pieces of geometry
// are respected. All three come from boards/<name>.terms by way of
// chips/i2c/eeproms.terms; none of them is hard-coded here.
//
//   PAGE     A write that would cross a page boundary does not continue into
//            the next page, it WRAPS to the start of the one it is in. So a
//            long write is split at page boundaries. Getting this wrong is
//            invisible on a short save and silently corrupts a long one --
//            the first bytes of the page come back holding the last.
//
//   BANK     The top address bits ride in the SLAVE address, not in the word
//            address: an M24M02 is four devices at 0xA0..0xA6. A transfer is
//            therefore also split at bank boundaries, and a sequential read
//            cannot stream past one.
//
//   WRITE    The part does not answer at all while it programs. Waiting is done
//            by ACK POLLING -- ask until it replies -- which takes as long as
//            the write actually took rather than the datasheet's worst case,
//            and which notices a part that has stopped answering for good.
//
// Nothing here writes in a loop for its own sake: one save is one pass over the
// data, and the polling below reads, never writes.

#include <stdint.h>
#include <stddef.h>
#include "csp_config.h"
#include "chip.h"

#if defined(CSP_EEPROM_I2C)

#ifndef CSP_EEPROM_BANK_BITS
#define CSP_EEPROM_BANK_BITS 0
#endif

// Bytes reachable without changing the slave address.
#define EE_BANK_SIZE (1UL << (8 * CSP_EEPROM_ADDR_BYTES))

// How many ACK polls to allow before calling the part dead. Each poll is a
// START, an address byte and a STOP -- about 30 us at 400 kHz -- so this is
// tens of milliseconds, several times the specified write cycle.
#ifndef CSP_EEPROM_POLL_TRIES
#define CSP_EEPROM_POLL_TRIES 2000
#endif

static int32_t ee_pos = -1;             // byte cursor; < 0 means closed

// The slave address for a byte offset: base, plus the bank bits shifted into
// the address field just above the R/W bit.
static uint8_t ee_slave(uint32_t addr)
{
#if CSP_EEPROM_BANK_BITS > 0
    uint32_t bank = (addr / EE_BANK_SIZE) & ((1u << CSP_EEPROM_BANK_BITS) - 1u);
    return (uint8_t)(CSP_EEPROM_I2C_ADDR | (bank << 1));
#else
    (void)addr;
    return (uint8_t)CSP_EEPROM_I2C_ADDR;
#endif
}

// The word address, big-endian -- most significant byte first, which is what
// every one of these parts expects and the opposite of the machine's own order.
static uint8_t ee_hdr(uint32_t addr, uint8_t *hdr)
{
    uint32_t off = addr % EE_BANK_SIZE;
    int i;

    for (i = CSP_EEPROM_ADDR_BYTES - 1; i >= 0; i--) {
	hdr[i] = (uint8_t)(off & 0xffu);
	off >>= 8;
    }
    return (uint8_t)CSP_EEPROM_ADDR_BYTES;
}

// Wait out a write cycle by asking until the part answers.
static int ee_wait_ready(uint8_t sla)
{
    int i;

    for (i = 0; i < CSP_EEPROM_POLL_TRIES; i++)
	if (Chip_I2C_MasterProbe(CSP_EEPROM_I2C_BUS, sla) == 0)
	    return 0;
    return -1;
}

// How much of `len`, starting at `addr`, can go in ONE transfer: stop at the
// next page boundary and at the next bank boundary, whichever comes first.
static uint32_t ee_chunk(uint32_t addr, uint32_t len, int writing)
{
    uint32_t room = EE_BANK_SIZE - (addr % EE_BANK_SIZE);

    if (writing) {
	uint32_t page = CSP_EEPROM_PAGE - (addr % CSP_EEPROM_PAGE);
	if (page < room)
	    room = page;
    }
    return (len < room) ? len : room;
}

const char* csp_eeprom_name(void)
{
    static const char nm[] = "EEPROM";
    return nm;
}

uint32_t csp_eeprom_capacity(void) { return CSP_EEPROM_BYTES; }

static int ee_open(void)
{
    Chip_I2C_Init(CSP_EEPROM_I2C_BUS);
    Chip_I2C_SetClockRate(CSP_EEPROM_I2C_BUS, CSP_EEPROM_HZ);
    // Is it actually there? Answering here means /save fails BEFORE it has
    // written half a program, and the failure names the bus rather than
    // arriving later as a corrupt image that will not load.
    if (Chip_I2C_MasterProbe(CSP_EEPROM_I2C_BUS, ee_slave(0)) < 0)
	return -1;
    ee_pos = 0;
    return 0;
}

int csp_eeprom_open_read(void)  { return ee_open(); }
int csp_eeprom_open_write(void) { return ee_open(); }

void csp_eeprom_close(void) { ee_pos = -1; }

int csp_eeprom_read(void* buf, size_t len)
{
    uint8_t* p = (uint8_t*)buf;
    uint32_t addr;
    uint32_t left;

    if (ee_pos < 0)
	return -1;
    if ((uint32_t)ee_pos + len > CSP_EEPROM_BYTES)
	return -1;

    addr = (uint32_t)ee_pos;
    left = (uint32_t)len;
    while (left) {
	uint8_t hdr[CSP_EEPROM_ADDR_BYTES];
	uint8_t hlen = ee_hdr(addr, hdr);
	uint32_t n = ee_chunk(addr, left, 0);

	if (Chip_I2C_MasterRead(CSP_EEPROM_I2C_BUS, ee_slave(addr),
				hdr, hlen, p, (uint16_t)n) < 0)
	    return -1;
	addr += n;
	p    += n;
	left -= n;
    }
    ee_pos = (int32_t)addr;
    return 0;
}

int csp_eeprom_write(const void* buf, size_t len)
{
    const uint8_t* p = (const uint8_t*)buf;
    uint32_t addr;
    uint32_t left;

    if (ee_pos < 0)
	return -1;
    // Refuse to run off the end. The address wraps in hardware, so a program
    // too big to persist would half-save and report success -- and the half
    // that wrapped would have overwritten its own beginning.
    if ((uint32_t)ee_pos + len > CSP_EEPROM_BYTES)
	return -1;

    addr = (uint32_t)ee_pos;
    left = (uint32_t)len;
    while (left) {
	uint8_t hdr[CSP_EEPROM_ADDR_BYTES];
	uint8_t hlen = ee_hdr(addr, hdr);
	uint32_t n = ee_chunk(addr, left, 1);
	uint8_t sla = ee_slave(addr);

	if (Chip_I2C_MasterWrite(CSP_EEPROM_I2C_BUS, sla,
				 hdr, hlen, p, (uint16_t)n) < 0)
	    return -1;
	// The part is programming now and will not answer. Wait for it HERE,
	// before the next chunk rather than after the last: a caller that
	// closes and powers down still leaves a finished write behind.
	if (ee_wait_ready(sla) < 0)
	    return -1;
	addr += n;
	p    += n;
	left -= n;
    }
    ee_pos = (int32_t)addr;
    return 0;
}

#endif /* CSP_EEPROM_I2C */
