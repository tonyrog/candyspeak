// Apply what the board description says: power, then pin functions.
//
// NOT csp_board_init -- that name is taken, by the runtime's own board hook in
// csp_lpcopen.c, and the two run in different PHASES. This one runs from
// SystemInit, before any driver exists; that one runs from main, after. Muxing
// pins from main would be too late: a driver initialised against an unpowered
// block has already written into nothing and reported success.
//
// ONE file for both families, which was not the plan and is the better answer.
// Chip_IOCON_PinMux(iocon, port, pin, mode, func) is LPCOpen's signature and it
// is what chips/nxp/drivers/212x/chip_212x.c implements too -- so the loop is
// identical and only the power register differs.
//
// Generated data, hand-written loop. The DATA is what changes per board and
// what a script can check (boards/<name>.terms, `make check-boards`); the loop
// is the same everywhere and belongs where it can be read.

#include <stdint.h>
#include "csp_config.h"
#include "chip.h"

// The first argument to Chip_IOCON_PinMux. The 212x header defines this to a
// null pointer (that family has no IOCON block); a real LPCOpen chip.h defines
// LPC_IOCON, so pick it up here.
#if !defined(LPC_IOCON_ARG)
#define LPC_IOCON_ARG LPC_IOCON
#endif

// --- the boot blink ----------------------------------------------------------
//
// The one signal that needs no UART, no correct baud rate and no terminal on
// the other end. It separates "the part is not running" from "the part is
// running and the console is wrong" -- otherwise one symptom with two causes,
// and an evening between them.
//
// Called from SystemInit BEFORE the pin mux: the LED is a GPIO at reset so it
// needs no mux, and going first means it also reports a fault in the mux.
//
// Three blinks, a COUNT rather than a steady state: a LED that is simply on
// could be a stuck pin, and one that is off could be anything at all.
#if defined(CSP_BOOT_LED_PORT)

// Roughly 150 ms, scaled to the core clock so the blink is the same LENGTH on
// a 60 MHz ARM7 and a 100 MHz Cortex-M. Which also makes the RATE a clock
// diagnostic: a PLL that did not connect makes it visibly many times slower.
#ifndef CSP_BOOT_BLINK_SPIN
#define CSP_BOOT_BLINK_SPIN (CSP_CORE_HZ / 40u)
#endif

// Did the last reset have a cause the hardware still remembers? Weak, because
// only some families keep one -- an LPC2000 has WDTOF and nothing else, a 17xx
// has a whole RSID register. A family that cannot say returns 0.
__attribute__((weak)) int csp_boot_fault(void) { return 0; }

static void led(int on)
{
    Chip_GPIO_SetPinState(LPC_GPIO, CSP_BOOT_LED_PORT, CSP_BOOT_LED_PIN,
			  on ? CSP_BOOT_LED_ON : !CSP_BOOT_LED_ON);
}

static void spin(uint32_t n)
{
    volatile uint32_t d;
    for (d = 0; d < n; d++)
	;
}

// Blink a NUMBER, without needing anything to work except the core and the pin.
//
// Deliberately NOT built on csp_delay_ms: that waits for the millisecond tick,
// which waits for an interrupt -- so it would report nothing at all in exactly
// the case one most wants a report, when interrupts are what is broken.
//
// A long lead-in flash, then n short ones, so the count is unmistakable even if
// you did not see it start.
void csp_boot_mark(int n)
{
    uint32_t unit = CSP_BOOT_BLINK_SPIN;
    int i;

    led(0);
    spin(unit * 3u);
    led(1);
    spin(unit * 4u);                    // lead-in: "a number follows"
    led(0);
    spin(unit * 2u);
    for (i = 0; i < n; i++) {
	led(1); spin(unit);
	led(0); spin(unit);
    }
    spin(unit * 3u);
}


// {boot_blink, false} in the board terms turns the report off. The pin is still
// set up and the LED still works -- the boot marks and the exception blink need
// it -- this only skips the four seconds at every boot, which a board with a
// working console does not need: the banner says the clock in figures.
#if defined(CSP_BOOT_BLINK_OFF)

void csp_boot_blink(void)
{
    Chip_GPIO_SetPinDIROutput(LPC_GPIO, CSP_BOOT_LED_PORT, CSP_BOOT_LED_PIN);
    led(0);
}

#else

void csp_boot_blink(void)
{
    uint32_t unit = CSP_BOOT_BLINK_SPIN;
    uint32_t mhz10;
    int i;

    Chip_GPIO_SetPinDIROutput(LPC_GPIO, CSP_BOOT_LED_PORT, CSP_BOOT_LED_PIN);

    // Three even blinks: "I am running".
    for (i = 0; i < 6; i++) {
	led((i & 1) == 0);
	spin(unit);
    }

    // Then the CLOCK, as a count of long flashes: one per 10 MHz, rounded.
    // 60 MHz is six, 12 MHz is one.
    //
    // This exists because the obvious way to report a clock -- print it -- goes
    // out of a UART whose divisor is derived from that same clock. When the
    // clock is wrong the message about it is unreadable, which is a circle you
    // cannot get out of from inside. The LED does not care what the clock is.
    //
    // Read back from the hardware (see Chip_SystemInit), so this is what the
    // part IS running at, not what the board asked for.
    led(0);
    spin(unit * 4u);                        // a gap, so the count starts clean

    mhz10 = (SystemCoreClock + 5000000u) / 10000000u;
    if (mhz10 == 0) mhz10 = 1;              // something is very wrong; say so
    if (mhz10 > 20) mhz10 = 20;             // and do not blink forever
    for (i = 0; i < (int)mhz10; i++) {
	led(1);
	spin(unit * 2u);                    // longer than the boot blink, so
	led(0);                             // the two cannot be confused
	spin(unit);
    }

    // A long double flash when the reset had a CAUSE the hardware remembers --
    // on an LPC2000 that means the watchdog, whose WDTOF bit is the only flag
    // that survives the reset it caused. A reset loop with no reported cause is
    // indistinguishable from a fault until something asks.
    if (csp_boot_fault()) {
	spin(unit * 4u);
	for (i = 0; i < 2; i++) {
	    led(1); spin(unit * 6u);
	    led(0); spin(unit * 2u);
	}
    }

    // Leave it OFF, which on an active-low wiring means driving the pin HIGH.
    // Ending on the wrong level is what once made this look like a solid lamp.
    led(0);
}

#endif

#else
// No LED named. A board that has not said which pin still builds.
void csp_boot_blink(void) { }
void csp_boot_mark(int n) { (void)n; }
#endif

// --- the bit-banged console --------------------------------------------------
//
// The one measurement that splits the remaining question in two. A board that
// will not talk is either a dead PIN -- wrong mux, wrong pin, a broken path to
// the RS232 driver -- or a misconfigured UART. Nothing read from inside can tell
// those apart, because a UART whose output never reaches a pin reports success
// at every step: THRE sets, the polling loop returns, the bytes are gone.
//
// So: drive the same pin as PLAIN GPIO and shift the bits out by hand. If text
// appears, the pin and the whole path to the terminal work, and the fault is in
// the peripheral -- and the register dump below then says which register.
// If nothing appears, everything in the UART driver is irrelevant.
//
// LPC2000 only, and only when asked for: `make ... DIAG=1`.
#if defined(CSP_BITBANG_DIAG) && defined(LPC_PINSEL)

#ifndef CSP_BITBANG_PORT
#define CSP_BITBANG_PORT 0
#endif
#ifndef CSP_BITBANG_PIN
#define CSP_BITBANG_PIN  0              // P0.0 is TXD0 on this family
#endif
#ifndef CSP_BITBANG_BAUD
#define CSP_BITBANG_BAUD CSP_LPC_BAUD
#endif

static void bb_level(int high)
{
    Chip_GPIO_SetPinState(LPC_GPIO, CSP_BITBANG_PORT, CSP_BITBANG_PIN, high);
}

// Wait until `nbits` bit times have passed since t0, computed from t0 each
// time rather than one delay per bit: 38400 baud is 26.04 us and the rounding
// of a per-bit delay accumulates into a frame that is a whole bit short by the
// stop bit. TIMER0 counts microseconds -- see Chip_Tick_Init.
static void bb_wait(uint32_t t0, uint32_t nbits)
{
    uint32_t target = (nbits * 1000000u) / CSP_BITBANG_BAUD;
    while ((Chip_Tick_Us() - t0) < target)
	;
}

static void bb_putc(uint8_t c)
{
    uint32_t t0 = Chip_Tick_Us();
    uint32_t nb = 0;
    int i;

    bb_level(0);                        // start bit
    bb_wait(t0, ++nb);
    for (i = 0; i < 8; i++) {           // 8 data bits, LSB first
	bb_level((c >> i) & 1);
	bb_wait(t0, ++nb);
    }
    bb_level(1);                        // stop, and one extra bit of idle
    bb_wait(t0, ++nb);
    bb_wait(t0, ++nb);
}

static void bb_puts(const char *s)
{
    while (*s) {
	if (*s == '\n')
	    bb_putc('\r');
	bb_putc((uint8_t)*s++);
    }
}

static void bb_hex(uint32_t v)
{
    static const char digits[] = "0123456789abcdef";
    int i;

    bb_puts("0x");
    for (i = 28; i >= 0; i -= 4)
	bb_putc((uint8_t)digits[(v >> i) & 0xfu]);
}

static void bb_kv(const char *name, uint32_t v)
{
    bb_puts(name);
    bb_putc('=');
    bb_hex(v);
    bb_putc('\n');
}

// Everything the UART's silence could be hiding, read back from the hardware
// and shipped out over a path that does not depend on the hardware in question.
void csp_bitbang_report(void)
{
    uint32_t irq;
    uint8_t  lcr, dll, dlm;

    // Interrupts off for the whole report. The millisecond tick lands every
    // 1000 us and a bit here is 26 us, so one ISR in the middle of a frame is
    // a stretched bit and a garbled character -- which would look exactly like
    // the wrong baud rate and send us chasing the wrong thing.
    irq = DisableIRQ();

    // Read the divisor before the pin moves, because reading it means toggling
    // DLAB, and DLAB left set is itself one of the candidate faults.
    lcr = (uint8_t)CSP_LPC_UART->LCR;
    CSP_LPC_UART->LCR = (uint8_t)(lcr | UART_LCR_DLAB);
    dll = (uint8_t)CSP_LPC_UART->DLL;
    dlm = (uint8_t)CSP_LPC_UART->DLM;
    CSP_LPC_UART->LCR = lcr;

    // The pin, as GPIO, idling high -- which is the mark level, the same thing
    // an idle TXD holds.
    Chip_IOCON_PinMux(LPC_IOCON_ARG, CSP_BITBANG_PORT, CSP_BITBANG_PIN, 0, 0);
    Chip_GPIO_SetPinDIROutput(LPC_GPIO, CSP_BITBANG_PORT, CSP_BITBANG_PIN);
    bb_level(1);
    bb_wait(Chip_Tick_Us(), 20);        // a couple of character times of idle

    bb_puts("\n--- bitbang ---\n");
    bb_kv("cclk   ", SystemCoreClock);
    bb_kv("pclk   ", Chip_Clock_GetPeripheralClockRate());
    bb_kv("vpbdiv ", LPC_VPBDIV);
    bb_kv("pllstat", LPC_PLLSTAT);
    bb_kv("memmap ", LPC_MEMMAP);
    bb_kv("pconp  ", LPC_PCONP);
    bb_kv("pinsel0", LPC_PINSEL->SEL0);
    bb_kv("pinsel1", LPC_PINSEL->SEL1);
    bb_kv("u.lcr  ", lcr);
    bb_kv("u.lsr  ", CSP_LPC_UART->LSR);
    bb_kv("u.ier  ", CSP_LPC_UART->IER);
    bb_kv("u.iir  ", CSP_LPC_UART->IIR);
    bb_kv("u.dll  ", dll);
    bb_kv("u.dlm  ", dlm);
    bb_puts("--- end ---\n");

    // Hand the pin back to the UART.
    Chip_IOCON_PinMux(LPC_IOCON_ARG, CSP_BITBANG_PORT, CSP_BITBANG_PIN, 0, 1);

    if (!(irq & 0x80u))                 // I bit clear = they were enabled
	EnableIRQ();
}

#else
void csp_bitbang_report(void) { }
#endif

#if defined(CSP_BOARD_PINS)

typedef struct { uint8_t port, bit, func; } board_pin_t;

static const board_pin_t board_pins[] = { CSP_BOARD_PINS };

void csp_board_pinmux(void)
{
    unsigned i;

    // POWER FIRST. A peripheral that is not clocked ignores writes to its
    // registers -- silently, no fault -- so muxing a pin to an unpowered block
    // leaves the pin doing nothing and the block looking broken.
    //
    // Written WHOLE rather than OR'd in: the reset value has several
    // peripherals on, and leaving those alone would mean a board can never turn
    // anything off. gen_chips.erl builds the word from {enable,...} with
    // everything else cleared.
#if defined(CSP_PCONP_VALUE)
#if defined(LPC_SYSCTL)
    LPC_SYSCTL->PCONP = CSP_PCONP_VALUE;     // 17xx and friends
#else
    LPC_PCONP = CSP_PCONP_VALUE;             // LPC2000: a bare address
#endif
#endif

    for (i = 0; i < CSP_BOARD_NPINS; i++)
	Chip_IOCON_PinMux(LPC_IOCON_ARG, board_pins[i].port,
			  board_pins[i].bit, 0, board_pins[i].func);
}

// Which ADC channel a `port:pin` names, from the board's own mux table.
//
// The default in csp_lpcopen.c is the identity -- `15:3` is channel 3 -- which
// is right when the .csp names channels. On a board with fixed screw terminals
// it is the wrong question: the program knows the connector, not the converter,
// so `in 0:25` should find channel 2 because that is how the pin is wired.
//
// Refusing an unmapped pin matters. Falling back to the identity would make
// `0:26` read channel 26, which does not exist, and the read would quietly
// return whatever the register held.
#if defined(CSP_BOARD_ADC)

typedef struct { uint8_t port, bit, chan; } board_adc_t;

static const board_adc_t board_adc[] = { CSP_BOARD_ADC };

int csp_lpc_adc_channel(uint8_t port, uint8_t pin)
{
    unsigned i;
    for (i = 0; i < CSP_BOARD_NADC; i++)
	if ((board_adc[i].port == port) && (board_adc[i].bit == pin))
	    return (int)board_adc[i].chan;
    return -1;                  // not an analog pin on this board
}

#endif

#else

// No generated description. Leave the pins at reset and let whatever set them
// up before carry on doing it -- a board that has not been described yet still
// builds and still runs.
void csp_board_pinmux(void) { }

#endif
