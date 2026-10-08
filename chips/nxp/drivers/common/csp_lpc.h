// The LPC family facts, shared by the port (port/csp_lpcopen.c) and the chip
// layer (csp_chip_lpc.c): which ADC, which UART status style, which EEPROM,
// which pin interrupts -- and the board knobs that pick a UART, an ADC and the
// pseudo-ports for ADC and DAC.
//
// One copy, because the two files have to agree. A converter width known to
// the chip layer and not to the port is how four analog inputs once read a
// quarter of their value.

#ifndef __CSP_LPC_H__
#define __CSP_LPC_H__

#include <stdint.h>
#include "csp_config.h"
#include "chip.h"

// --- which family -----------------------------------------------------------
// chip.h has already been included by now, so the family can be recognised from
// what IT defined rather than from a flag we ask the build to pass. The families
// differ in three places only -- ADC, UART status, EEPROM -- and each is guarded
// by one of these.
#if defined(CHIP_LPC177X_8X) || defined(CHIP_LPC40XX)
#define CSP_LPC_ADC_CLASSIC   1     // Chip_ADC_Init(pADC, &ADC_CLOCK_SETUP_T)
#define CSP_LPC_UART_LSR      1     // Chip_UART_ReadLineStatus + UART_LSR_*
#define CSP_LPC_EEPROM_PAGED  1     // Chip_EEPROM_Read/Write(page, offset, ...)
#define CSP_LPC_GPIOINT       1     // GPIO interrupts on ports 0 and 2
#elif defined(CHIP_LPC175X_6X)
// Same peripherals as its 177x/8x sibling with ONE exception that matters here:
// no EEPROM. eeprom_17xx_40xx.h is in the 175x_6x driver directory, which makes
// it look otherwise -- but chip_lpc175x_6x.h never defines LPC_EEPROM, so a
// paged-EEPROM build fails to compile rather than misbehaving. (LPC1754 is this
// part, and there is a Makefile for it.)
#define CSP_LPC_ADC_CLASSIC   1
#define CSP_LPC_UART_LSR      1
#define CSP_LPC_NO_EEPROM     1
// And it spells them differently. The 175x library calls the first uart
// LPC_UART0 and its one converter LPC_ADC; the 18xx/43xx one says LPC_USART0
// and LPC_ADC0. Both are LPCOpen, neither is wrong, and the default further
// down happens to be the other family's -- so say it here rather than make
// every 175x board carry two defines that have nothing to do with the board.
#define CSP_LPC_UART_DEFAULT  LPC_UART0
#define CSP_LPC_ADC_DEFAULT   LPC_ADC
// Any pin on port 0 or 2 can interrupt, with no pin function to select. The
// 177x/8x above has the same block; the 212x below has none and uses EINT.
#define CSP_LPC_GPIOINT       1
#elif defined(CHIP_LPC18XX) || defined(CHIP_LPC43XX)
#define CSP_LPC_ADC_CLASSIC   1
#define CSP_LPC_UART_LSR      1
#define CSP_LPC_EEPROM_MAPPED 1     // memory-mapped at EEPROM_ADDRESS
#elif defined(CHIP_LPC15XX)
#define CSP_LPC_ADC_SEQ       1     // sequencer-based ADC, different API
#define CSP_LPC_UART_STAT     1     // Chip_UART_GetStatus + UART_STAT_*
#define CSP_LPC_EEPROM_IAP    1     // through the IAP ROM calls
#elif defined(CHIP_LPC212X)
// ARM7, and it has to say so. Without a define of its own this family fell
// through to the `#else` below and was configured as an 11xx: a 12-BIT
// converter on a part whose ADC is 10 bits, so every reading came back a
// quarter of its true value with nothing to say why.
//
// That is the cost of a final #else that names parts rather than describing a
// default: it accepts anything, including a family nobody considered.
#define CSP_LPC_ADC_CLASSIC   1
#define CSP_LPC_UART_LSR      1
#define CSP_LPC_ADC_BITS      10    // ADGDR holds 10 bits, left-justified at 6
#define CSP_LPC_UART_DEFAULT  LPC_UART0
#define CSP_LPC_ADC_DEFAULT   LPC_ADC
// No on-chip EEPROM. A board with an I2C part says so in its terms and gets
// csp_eeprom_i2c.c instead; this only means the chip has none of its own.
#define CSP_LPC_NO_EEPROM     1
// NO GPIO INTERRUPTS on this family -- that block arrived with the 17xx. Pin
// interrupts here are the four EINTs, and they are a PIN FUNCTION: the pin has
// to be muxed to eintN, which the board file does.
#define CSP_LPC_EINT          1
#else                                // 11xx, 11u6x, 13xx
#define CSP_LPC_ADC_CLASSIC   1
#define CSP_LPC_UART_LSR      1
#define CSP_LPC_NO_EEPROM     1     // flash-only parts: /save has nowhere to go
#endif

// The first argument to Chip_IOCON_PinMux. The 212x header defines it to a null
// pointer -- that family has no IOCON block, PINSEL is a handful of addresses --
// and a real LPCOpen chip.h has LPC_IOCON. Same fallback as csp_board.c.
#if !defined(LPC_IOCON_ARG)
#define LPC_IOCON_ARG LPC_IOCON
#endif

// --- board knobs ------------------------------------------------------------
#ifndef CSP_LPC_UART_DEFAULT
#define CSP_LPC_UART_DEFAULT  LPC_USART0
#endif
#ifndef CSP_LPC_ADC_DEFAULT
#define CSP_LPC_ADC_DEFAULT   LPC_ADC0
#endif

#ifndef CSP_LPC_UART
#define CSP_LPC_UART      CSP_LPC_UART_DEFAULT
#endif
#ifndef CSP_LPC_BAUD
#define CSP_LPC_BAUD      115200
#endif
#ifndef CSP_LPC_ADC
#define CSP_LPC_ADC       CSP_LPC_ADC_DEFAULT
#endif
#ifndef CSP_LPC_ADC_RATE
#define CSP_LPC_ADC_RATE  400000     // ADC clock; 400 kHz is the usual max
#endif
#ifndef CSP_LPC_ADC_BITS
#define CSP_LPC_ADC_BITS  12         // 10 on the 11xx parts
#endif
#ifndef CSP_LPC_ADC_PORT
#define CSP_LPC_ADC_PORT  15         // an #analog here reads an ADC channel
#endif
#ifndef CSP_LPC_DAC_PORT
#define CSP_LPC_DAC_PORT  13         // ...and here it writes the DAC
#endif

// --- the seams ----------------------------------------------------------------
// Board wiring, implemented in csp_chip_lpc.c as stubs or weak defaults and
// replaced by a board or chip file that knows better. See the comments there.
extern void csp_lpc_pin_mux(uint8_t port, uint8_t pin, int analog);
extern int  csp_lpc_adc_channel(uint8_t port, uint8_t pin);
extern void csp_lpc_pwm_write(uint8_t port, uint8_t pin, int val);
extern void csp_lpc_dac_write(uint8_t pin, int val);
extern void csp_lpc_board_init(void);
extern void csp_pwm_init(void);
extern int  csp_lpc_can_ok(void);           // the bus came up in csp_chip_init
extern int  csp_chip_can_send(uint32_t id, const uint8_t* d, uint8_t len);

// The tick seam: Cortex-M implements it over SysTick (csp_chip_lpc.c), the
// ARM7 over a timer match (chip_212x.c). Counting UP on both -- see
// chip_212x.h.
extern void     Chip_Tick_Init(uint32_t hz);
extern uint32_t Chip_Tick_Us(void);
extern void     csp_tick_isr(void);

#endif
