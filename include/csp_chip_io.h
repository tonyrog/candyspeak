// The chip layer: what a part does with a pin, and nothing about CandySpeak.
//
// No csp_rt_t, no value_t, no declaration table. A port and pin go in, a value
// comes out (or goes in), and that is the whole contract -- so the same calls
// serve the runtime's I/O sweep (port/csp_io.c) and C written by hand or
// generated from a .csp, which has the port and pin as constants and nothing
// to look them up in.
//
// One library per chip family, under chips/<vendor>/drivers/. What the runtime
// adds on top -- the declared width, signedness, a rule turning a pin round,
// interrupts as `fired` -- lives in port/csp_io.c, not here.
//
// ANALOG IS SIXTEEN BITS, both ways, whatever the converter has. A 10-bit ADC
// returns its reading shifted up by six; a PWM channel scales 0..65535 onto its
// own period. That is what lets ONE scaling rule above this layer serve every
// part, instead of each port reading the declaration to find out what width
// it was asked for.

#ifndef __CSP_CHIP_IO_H__
#define __CSP_CHIP_IO_H__

#include <stdint.h>

// The same values as the runtime's DIR_IN/DIR_OUT, so a direction read out of
// a declaration passes straight through. Spelled again here because this
// header must not need csp.h; port/csp_io.c checks that they agree.
#define CSP_CHIP_IN       0x01
#define CSP_CHIP_OUT      0x02

#define CSP_CHIP_PULLUP   0x01
#define CSP_CHIP_PULLDOWN 0x02

// Clocks, timers and the converters. Before anything else.
extern void     csp_chip_init(void);

// Free-running milliseconds since csp_chip_init.
extern uint32_t csp_chip_millis(void);

// dir is CSP_CHIP_IN or CSP_CHIP_OUT; IN|OUT configures it as an input, which
// is where a bidirectional pin rests between writes. pull is ignored for an
// output, and a pull the part does not have is dropped rather than inverted.
extern void     csp_chip_dcfg(uint8_t port, uint8_t pin, uint8_t dir,
			      uint8_t pull);
extern int      csp_chip_din(uint8_t port, uint8_t pin);
extern void     csp_chip_dout(uint8_t port, uint8_t pin, int v);

// pwm selects a PWM output over a DAC where a pin could be either.
extern void     csp_chip_acfg(uint8_t port, uint8_t pin, uint8_t dir,
			      uint8_t pwm);
extern uint16_t csp_chip_ain(uint8_t port, uint8_t pin);
extern void     csp_chip_aout(uint8_t port, uint8_t pin, uint16_t v);

// One received CAN frame, or 0 when there is none waiting. `id' is the
// identifier as the program writes it (`#buffer B:8 in can 0x30'), `len' the
// data length, d up to eight bytes. A chip with no CAN -- or a board whose
// program never asks -- gets the weak one in src/csp_lib.c, which has none.
extern int      csp_chip_can_recv(uint32_t* id, uint8_t* d, uint8_t* len);

#endif
