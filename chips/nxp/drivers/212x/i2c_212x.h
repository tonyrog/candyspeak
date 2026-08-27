#ifndef __I2C_212X_H__
#define __I2C_212X_H__

// LPC2000 I2C master, POLLED.
//
// The working driver for this board (seazone/src/target_lpc/i2c_lpc.c) is an
// interrupt-driven state machine with a queue, 560 lines, and that is the right
// shape when transfers overlap other work. Here they do not: the only client is
// the EEPROM, and it is touched at boot, on /save and on /load -- never in the
// reactive cycle. A polled master is the same status codes in a loop, with no
// ISR, no queue and no reentrancy question.
//
// If a second client ever appears that cannot wait -- a sensor read inside the
// cycle -- this is the file to replace, and the interface below does not have
// to change to do it.

#include <stdint.h>

// EIGHT-BIT registers four bytes apart, like the UART. See chip_212x.h on why
// that is not cosmetic. SCLH and SCLL are the exceptions: genuinely 16-bit.
typedef struct {
    __IO uint8_t  CONSET;           // 0x00 set bits (write 1 = set)
    uint8_t  _pad0[3];
    __I  uint8_t  STAT;             // 0x04 status, the state machine's answer
    uint8_t  _pad1[3];
    __IO uint8_t  DAT;              // 0x08 data
    uint8_t  _pad2[3];
    __IO uint8_t  ADR;              // 0x0C own slave address
    uint8_t  _pad3[3];
    __IO uint16_t SCLH;             // 0x10 clock high period, in pclk cycles
    uint8_t  _pad4[2];
    __IO uint16_t SCLL;             // 0x14 clock low period
    uint8_t  _pad5[2];
    __O  uint8_t  CONCLR;           // 0x18 clear bits (write 1 = clear)
    uint8_t  _pad6[3];
} LPC_I2C_T;

#define LPC_I2C0 ((LPC_I2C_T *) 0xE001C000)
#define LPC_I2C1 ((LPC_I2C_T *) 0xE005C000)   // not on a 2119/2129

#define I2C_CON_AA   (1u << 2)      // assert acknowledge
#define I2C_CON_SI   (1u << 3)      // serial interrupt: the state machine waits
#define I2C_CON_STO  (1u << 4)      // stop
#define I2C_CON_STA  (1u << 5)      // start
#define I2C_CON_EN   (1u << 6)      // interface enable

// Master status codes, straight out of the user manual. Named because a bare
// 0x18 in a comparison says nothing about what went wrong when it is 0x20.
#define I2C_ST_START     0x08       // START transmitted
#define I2C_ST_RSTART    0x10       // repeated START transmitted
#define I2C_ST_SLAW_ACK  0x18       // SLA+W sent, ACK
#define I2C_ST_SLAW_NAK  0x20       // SLA+W sent, NACK -- nobody there
#define I2C_ST_DATW_ACK  0x28       // data sent, ACK
#define I2C_ST_DATW_NAK  0x30       // data sent, NACK
#define I2C_ST_ARBLOST   0x38       // arbitration lost
#define I2C_ST_SLAR_ACK  0x40       // SLA+R sent, ACK
#define I2C_ST_SLAR_NAK  0x48       // SLA+R sent, NACK
#define I2C_ST_DATR_ACK  0x50       // data received, we ACKed
#define I2C_ST_DATR_NAK  0x58       // data received, we NACKed (the last one)

// 0 on success, negative on failure. `sla` is the 8-bit address with the R/W
// bit ZERO -- 0xA0 for an EEPROM -- the way the datasheets write it.
void Chip_I2C_Init(LPC_I2C_T *i2c);
void Chip_I2C_SetClockRate(LPC_I2C_T *i2c, uint32_t hz);

// Header then payload, in ONE transfer with no STOP between them.
//
// That split is what memory devices need and what a plain "write these bytes"
// call cannot express: the header is the internal address, the payload is the
// data, and a STOP in between would latch the address and then start an
// unrelated transfer. Same shape as i2c_read/i2c_write in the old driver.
// hdr may be NULL with hlen 0.
int  Chip_I2C_MasterWrite(LPC_I2C_T *i2c, uint8_t sla,
			  const uint8_t *hdr, uint8_t hlen,
			  const uint8_t *buf, uint16_t len);

// Header (written), then a REPEATED START and the payload read back.
int  Chip_I2C_MasterRead(LPC_I2C_T *i2c, uint8_t sla,
			 const uint8_t *hdr, uint8_t hlen,
			 uint8_t *buf, uint16_t len);

// Does anybody answer at this address? START, SLA+W, STOP.
//
// This is ACK POLLING, which is how you wait out an EEPROM write cycle: the
// device does not answer while it is busy programming, so asking until it does
// takes exactly as long as the write took and not the datasheet's worst case.
int  Chip_I2C_MasterProbe(LPC_I2C_T *i2c, uint8_t sla);

#endif
