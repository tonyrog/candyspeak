// LPC2000 drivers in the LPCOpen idiom. See chip_212x.h for why.
//
// Each of these is a handful of register writes -- the LPC2000 peripherals are
// simple, which is why the layer this replaces (a driver struct with function
// pointers, nxp_files/2129/*_lpc.c) cost more than it bought. There is one
// UART driver and one caller; an indirection between them buys nothing.

#include <stdint.h>
#include "csp_config.h"
#include "chip_212x.h"
#include "vic_212x.h"

// Defined by the platform file; the chip layer decides how it gets called.
void csp_tick_isr(void);

static uint32_t cclk_rate = 0;      // core clock, Hz
static uint32_t pclk_rate = 0;      // peripheral bus clock, Hz

// --- clock ------------------------------------------------------------------

// The PLL takes a two-write FEED sequence to accept anything. Miss it and the
// write is silently ignored -- no fault, no flag, the part simply keeps running
// on the crystal. It is the single most common way an LPC2000 boots at 1/5 the
// clock you thought you asked for.
static void pll_feed(void)
{
    LPC_PLLFEED = 0xAA;
    LPC_PLLFEED = 0x55;
}

// Fcco = 2 * M * Fosc * N. M is the multiplier (1..32, written as M-1), N the
// divider (1, 2, 4, 8, written as log2). Fcco must land in 156..320 MHz.
//
// Solved HERE for now; the plan is to solve it in gen_chips.erl from the
// board's crystal and hand this the answer, so an unreachable clock fails the
// build instead of running slow.
static uint8_t reset_cause;

uint8_t Chip_ResetCause(void) { return reset_cause; }

// Dead cycles after a change that alters how instructions are FETCHED.
//
// The ARM7 pipeline has already loaded the next instructions, and with the MAM
// involved they may have been prefetched under the old rules and be about to
// execute under the new ones. The working code for this board does exactly this
// after touching MAMCR -- three volatile writes and a wait -- and it is the
// same reason VPBDIV is written twice.
//
// volatile, so they cannot be optimised away: their only purpose is to take up
// time and fetch slots.
static void settle(void)
{
    volatile uint32_t dummy;
    dummy = 1;
    dummy = 2;
    dummy = 3;
    (void)dummy;
}

// The two-write feed the watchdog needs, same idea as the PLL's.
static void wdt_feed(void)
{
    LPC_WDFEED = 0xAA;
    LPC_WDFEED = 0x55;
}

void Chip_SystemInit(uint32_t xtal_hz, uint32_t cclk_hz, uint32_t pclk_div)
{
    uint32_t m, n;

    // WHERE THE EXCEPTION VECTORS COME FROM. This has to be said, and nothing
    // else will say it.
    //
    // MEMMAP remaps the bottom 64 bytes -- the vector table -- to one of three
    // places: the boot block (0), user flash (1), or RAM (2). A part that
    // arrived here from the ISP boot loader can still be in boot-loader mode,
    // and then OUR vector table at 0x00000000 is not the one the core reads.
    //
    // The reset vector already ran, so the program is executing and everything
    // looks fine. It stays fine right up until the first INTERRUPT, which
    // fetches its vector from the boot block and lands somewhere that was never
    // meant to handle it. No fault, no reset -- the part simply stops, after
    // running long enough to look healthy.
    //
    // Which is exactly what a board does when it blinks its boot LED, reaches
    // main, and then goes silent before it can print anything.
    LPC_MEMMAP = MEMMAP_USER_FLASH;

    // Then: what state did we come up in?
    reset_cause = (uint8_t)LPC_WDMOD;

    // If the watchdog is running, it is running for good -- WDEN cannot be
    // cleared by software. The only choice is to feed it or be reset, so wind
    // the counter to its maximum and feed. Doing this before the clock moves
    // matters: the timeout is in peripheral clock cycles, and raising cclk
    // shortens it.
    if (reset_cause & WDMOD_WDEN) {
	LPC_WDTC = 0x00FFFFFFu;
	wdt_feed();
    }

    // Memory accelerator off while the clock moves: its timing is expressed in
    // cclk cycles, so a MAMTIM that was right for the crystal is wrong the
    // instant the PLL connects.
    LPC_MAMCR = MAMCR_OFF;
    settle();

    m = (xtal_hz && cclk_hz) ? (cclk_hz / xtal_hz) : 1;
    if (m < 1) m = 1;
    if (m > 32) m = 32;

    // The smallest N that puts Fcco in range. 2*M*Fosc*N, so N doubles it.
    for (n = 1; n <= 8; n <<= 1) {
	uint32_t fcco = 2u * m * xtal_hz * n;
	if ((fcco >= 156000000u) && (fcco <= 320000000u))
	    break;
    }
    if (n > 8) n = 1;               // nothing fits: run on the crystal

    LPC_PLLCFG = (uint8_t)((m - 1) | (((n == 1) ? 0 :
				       (n == 2) ? 1 :
				       (n == 4) ? 2 : 3) << 5));
    pll_feed();
    LPC_PLLCON = PLLCON_PLLE;
    pll_feed();

    // BOUNDED. An unbounded wait here is how a board that will not lock its PLL
    // becomes a board that does nothing at all, forever, with no way to tell
    // that from a dozen other faults. The same fix was made on the Cortex-M
    // side; this one was missed.
    //
    // The count is loop iterations at up to 60 MHz, so this is milliseconds --
    // far longer than the ~100 us a PLL needs to lock.
    {
	uint32_t w;
	for (w = 1000000u; w && !(LPC_PLLSTAT & PLLSTAT_LOCK); w--)
	    ;
	if (w) {
	    LPC_PLLCON = PLLCON_PLLE | PLLCON_PLLC;
	    pll_feed();
	}
    }

    // VPBDIV: 0 = cclk/4, 1 = cclk/1, 2 = cclk/2. Not the divisor itself.
    //
    // WRITTEN TWICE, and that is not belt-and-braces. Several LPC2000 silicon
    // revisions have an erratum where a single write to VPBDIV does not take --
    // the register reads back the new value while the divider keeps the old
    // one. Reading it back therefore does NOT catch this, which is why the
    // readback below is not enough on its own.
    //
    // The symptom is the nastiest kind of quiet: software believes pclk is
    // cclk, hardware is still dividing by four, and every rate derived from it
    // -- the UART divisor above all -- is off by exactly that factor. The port
    // transmits perfectly at a quarter of the intended baud.
    //
    // The working code for this board does the same thing, written as an
    // innocent-looking initialisation: `APBDIV = 1;` and then `APBDIV =
    // APB_CCLK;`, which is also 1. Twice, every time, in both places it
    // touches the register.
    {
	uint8_t v = (uint8_t)((pclk_div == 1) ? 1 : (pclk_div == 2) ? 2 : 0);
	LPC_VPBDIV = v;
	settle();
	LPC_VPBDIV = v;
	settle();
    }

    // --- and now READ BACK what actually happened ---------------------------
    //
    // Not what we asked for. Every rate downstream is derived from these two --
    // the UART divisor, the 1 MHz timer prescale, CAN bit timing -- so if the
    // software's idea of the clock ever disagrees with the hardware's, every
    // one of them is wrong by the same factor and the symptom is garbage on the
    // wire with nothing to say why.
    //
    // The two ways they can disagree are both silent. A PLL FEED that does not
    // land leaves the write ignored, no fault and no flag: PLLSTAT's PLLC bit
    // is the only thing that knows whether the connect took. And VPBDIV is a
    // plain register, so reading it back costs nothing and closes the same gap.
    //
    // With this, a failure changes the reported CLOCK rather than corrupting
    // everything derived from it -- so a wrong clock stays legible instead of
    // becoming noise.
    cclk_rate = (LPC_PLLSTAT & PLLSTAT_PLLC) ? (xtal_hz * m) : xtal_hz;

    switch (LPC_VPBDIV & 3u) {
    case 1:  pclk_rate = cclk_rate;     break;
    case 2:  pclk_rate = cclk_rate / 2; break;
    default: pclk_rate = cclk_rate / 4; break;
    }

    // One flash access per MAMTIM cclk cycles, rounded UP -- too few is a part
    // that reads stale instructions, which does not fault, it misbehaves.
    //
    // Plus one. At 60 MHz the formula gives exactly 3, which is the minimum and
    // therefore has no margin at all: flash access time is a typical figure,
    // not a guaranteed one, and one extra cycle costs a few percent of speed
    // against a failure mode that looks like random corruption.
    LPC_MAMTIM = (uint8_t)((cclk_rate + 19999999u) / 20000000u) + 1u;

    // PARTIAL, not full, and this is deliberate.
    //
    // NXP's errata for the LPC2109/2119/2129 documents the fully-enabled MAM
    // fetching incorrectly on several silicon revisions, and partial mode is
    // the published workaround. Full mode prefetches branch targets as well as
    // sequential code; partial does only the sequential half, which is most of
    // the benefit.
    //
    // The failure it avoids is the nastiest kind: no fault, no flag, just an
    // occasional wrong instruction. Code runs, mostly. A board that knows its
    // revision is good can ask for full.
#if defined(CSP_MAM_FULL)
    LPC_MAMCR = MAMCR_FULL;
    settle();
#else
    LPC_MAMCR = MAMCR_PARTIAL;
    settle();
#endif
}

uint32_t Chip_Clock_GetSystemClockRate(void)     { return cclk_rate; }
uint32_t Chip_Clock_GetPeripheralClockRate(void) { return pclk_rate; }

// --- UART -------------------------------------------------------------------

void Chip_UART_Init(LPC_USART_T *u)
{
    volatile uint8_t junk;

    u->LCR = 0x03;                  // 8N1, DLAB clear
    u->IER = 0;

    // Read the status registers away. IIR, RBR and LSR all latch and clear on
    // read, and this part arrives here from the ISP boot loader, which has been
    // using this very UART -- so there is a pending interrupt id, a received
    // byte and a line status sitting in it that belong to somebody else's
    // session. The working driver for this board does the same three reads.
    junk = u->IIR;
    junk = u->RBR;
    junk = u->LSR;
    (void)junk;

    u->FCR = (uint8_t)(UART_FCR_FIFO_EN | UART_FCR_RX_RS | UART_FCR_TX_RS);
}

void Chip_UART_SetBaud(LPC_USART_T *u, uint32_t baud)
{
    // The classic divisor. No fractional divider on this part -- that arrived
    // with the 17xx -- so the baud rate is whatever pclk/(16*div) lands on.
    //
    // ROUNDED TO NEAREST, not truncated. At 60 MHz and 38400 the exact answer
    // is 97.65: truncating gives +0.68% and rounding gives -0.35%, so it is
    // twice as close for one comparison. The working driver for this board does
    // the same, and on a part with no fractional divider that half is the only
    // accuracy available.
    uint32_t b16 = 16u * (baud ? baud : 9600u);
    uint32_t div = pclk_rate / b16;
    uint8_t lcr;

    if ((pclk_rate % b16) > (b16 / 2u))
	div++;
    lcr = (uint8_t)u->LCR;

    u->LCR = lcr | UART_LCR_DLAB;
    u->DLL = (uint8_t)(div & 0xff);
    u->DLM = (uint8_t)((div >> 8) & 0xff);
    u->LCR = lcr;                   // clearing DLAB is what makes RBR/THR live
}

void Chip_UART_ConfigData(LPC_USART_T *u, uint32_t cfg) { u->LCR = (uint8_t)(cfg & 0x7f); }
void Chip_UART_SetupFIFOS(LPC_USART_T *u, uint32_t cfg) { u->FCR = (uint8_t)cfg; }
void Chip_UART_TXEnable(LPC_USART_T *u) { (void)u; }   // always enabled here
void Chip_UART_Enable(LPC_USART_T *u)   { (void)u; }

void Chip_UART_SendByte(LPC_USART_T *u, uint8_t b)
{
    while (!(u->LSR & UART_LSR_THRE))
	;
    u->THR = b;
}

uint8_t  Chip_UART_ReadByte(LPC_USART_T *u)       { return (uint8_t)u->RBR; }
uint32_t Chip_UART_ReadLineStatus(LPC_USART_T *u) { return u->LSR; }
uint32_t Chip_UART_GetStatus(LPC_USART_T *u)      { return u->LSR; }

// --- GPIO -------------------------------------------------------------------
// `port` selects the block, so the LPC_GPIO_T argument is ignored -- kept for
// signature compatibility with the 17xx driver the platform file also calls.

void Chip_GPIO_Init(LPC_GPIO_T *g) { (void)g; }

void Chip_GPIO_SetPinDIROutput(LPC_GPIO_T *g, uint8_t port, uint8_t pin)
{
    (void)g;
    LPC_GPIO_PORT(port)->DIR |= (1u << pin);
}

void Chip_GPIO_SetPinDIRInput(LPC_GPIO_T *g, uint8_t port, uint8_t pin)
{
    (void)g;
    LPC_GPIO_PORT(port)->DIR &= ~(1u << pin);
}

// SET and CLR, never a read-modify-write of PIN: two rules driving two pins of
// the same port in one cycle would otherwise race, each writing back the whole
// word it read.
void Chip_GPIO_SetPinState(LPC_GPIO_T *g, uint8_t port, uint8_t pin, int on)
{
    (void)g;
    if (on)
	LPC_GPIO_PORT(port)->SET = (1u << pin);
    else
	LPC_GPIO_PORT(port)->CLR = (1u << pin);
}

int Chip_GPIO_GetPinState(LPC_GPIO_T *g, uint8_t port, uint8_t pin)
{
    (void)g;
    return (LPC_GPIO_PORT(port)->PIN >> pin) & 1u;
}

// --- pin function -----------------------------------------------------------

void Chip_IOCON_PinMux(void *iocon, uint8_t port, uint8_t pin,
		       uint16_t mode, uint8_t func)
{
    volatile uint32_t *sel;
    uint8_t shift;

    (void)iocon;
    (void)mode;                     // no per-pin pull config on this family

    if (port == 0) {
	sel = (pin < 16) ? &LPC_PINSEL->SEL0 : &LPC_PINSEL->SEL1;
	shift = (uint8_t)((pin & 15) * 2);
    } else if (port == 1) {
	// PINSEL2 is not two-bits-per-pin: it is a handful of enables for the
	// debug and trace ports. Anything else on P1 is GPIO and needs no
	// write, so the safe answer is to leave it alone.
	return;
    } else {
	return;
    }
    *sel = (*sel & ~(3u << shift)) | (((uint32_t)func & 3u) << shift);
}

// --- ADC --------------------------------------------------------------------

void Chip_ADC_Init(LPC_ADC_T *a, void *setup)
{
    (void)setup;
    // CLKDIV so the ADC clock stays at or under 4.5 MHz, and PDN to wake it.
    uint32_t div = (pclk_rate + 4499999u) / 4500000u;
    if (div) div -= 1;
    if (div > 255) div = 255;
    a->CR = ADC_CR_PDN | ((div & 0xff) << 8);
}

void Chip_ADC_SetSampleRate(LPC_ADC_T *a, void *setup, uint32_t rate)
{
    (void)a; (void)setup; (void)rate;
    // Fixed by CLKDIV above: this part converts in 11 ADC clocks and has no
    // rate control beyond that.
}

// The result the poll caught and the read has not collected yet. See
// Chip_ADC_ReadStatus below for why it has to be held rather than re-read.
static uint32_t adc_held;

// Changing which channel is selected CLEARS START first, always.
//
// The user manual is explicit that START must be 000 while SEL changes, and the
// reason is visible in the register: SEL and START live in the same word, so a
// read-modify-write that touches SEL while START still says "now" begins a
// conversion on whatever SEL happens to hold at that instant. Between two reads
// that is the OLD channel and the NEW one both selected -- and the converter
// takes the lowest, which is not the one anybody asked for.
//
// The old code left START set from the previous read, so every channel change
// fired a spurious conversion on the wrong input and left its result sitting in
// ADGDR for the next poll to find. With one result register and no channel
// check that is simply the wrong number; with the channel check it is a wasted
// conversion and a stale value the poll has to wait past.
static void adc_sel(LPC_ADC_T *a, uint32_t sel)
{
    a->CR = (a->CR & ~((7u << 24) | 0xffu)) | (sel & 0xffu);
}

void Chip_ADC_EnableChannel(LPC_ADC_T *a, uint8_t ch, int enable)
{
    uint32_t sel = a->CR & 0xffu;

    if (enable)
	sel |= (1u << (ch & 7));
    else
	sel &= ~(1u << (ch & 7));
    adc_sel(a, sel);
    // Anything the previous conversion left behind belongs to the old channel.
    adc_held = 0;
    (void)a->GDR;                       // read to clear DONE
}

void Chip_ADC_SetStartMode(LPC_ADC_T *a, uint8_t mode, uint8_t edge)
{
    (void)edge;
    a->CR = (a->CR & ~(7u << 24)) | (((uint32_t)mode & 7u) << 24);
}

// ONE result register, and reading it CLEARS the DONE flag.
//
// That breaks the poll-then-read pair the LPCOpen API is shaped around: the
// poll would consume the very result the read is about to ask for, and the read
// would then find DONE clear and report failure -- forever, since the poll loop
// keeps eating each new conversion. On a 17xx this does not arise, because
// there each channel has its own DR and the flag survives until that one is
// read.
//
// So the poll CATCHES the result and holds it. The pair still behaves the way
// every caller expects; the caching is the price of a read-clearing register.

int Chip_ADC_ReadStatus(LPC_ADC_T *a, uint8_t ch, uint32_t what)
{
    uint32_t d;

    (void)what;
    if (adc_held & ADC_DR_DONE)         // already caught, not yet collected
	return 1;
    d = a->GDR;                         // this read clears DONE in the hardware
    if (!(d & ADC_DR_DONE))
	return 0;
    // Whose result is it? With one conversion in flight it is always the
    // channel just started -- unless something else started one, and then a
    // silently wrong reading is the worst possible outcome.
    if (ADC_DR_CHN(d) != (ch & 7u))
	return 0;
    adc_held = d;
    return 1;
}

int Chip_ADC_ReadValue(LPC_ADC_T *a, uint8_t ch, uint16_t *out)
{
    uint32_t d = adc_held;

    if (!(d & ADC_DR_DONE)) {           // read without polling first
	d = a->GDR;
	if (!(d & ADC_DR_DONE) || (ADC_DR_CHN(d) != (ch & 7u)))
	    return 0;
    }
    adc_held = 0;
    *out = (uint16_t)((d >> 6) & 0x3ff);   // 10 bits, left-justified at bit 6
    return 1;
}

// --- timer ------------------------------------------------------------------

void Chip_TIMER_Init(LPC_TIMER_T *t)
{
    t->TCR = TIMER_TCR_RESET;
    t->PR  = 0;
    t->MCR = 0;
    t->IR  = 0xff;                  // write 1 to clear
}

void Chip_TIMER_SetMatch(LPC_TIMER_T *t, uint8_t n, uint32_t v)
{
    t->MR[n & 3] = v;
}

void Chip_TIMER_Enable(LPC_TIMER_T *t) { t->TCR = TIMER_TCR_EN; }
uint32_t Chip_TIMER_ReadCount(LPC_TIMER_T *t) { return t->TC; }

// --- the tick ---------------------------------------------------------------

uint32_t SystemCoreClock = 0;

void SystemCoreClockUpdate(void) { SystemCoreClock = cclk_rate; }

// Counts of the 1 MHz timer between ms interrupts. A constant, but named:
// `+ 1000` in the reschedule would be a number nobody could grep for.
#define TICK_US 1000u

// The match register the tick uses. NOT MR0: that is MAT0.0, which is a PWM
// output on this board -- see the note in chip_212x.h. MR1 and MR3 are left
// alone for the same reason.
#define TICK_MR 2
#define TICK_MR2I (1u << 6)             // MCR: interrupt on MR2, no reset

// Clear the match flag FIRST, then reschedule, then do the work. The other
// order leaves a window where the timer can match again before the flag is
// cleared, and that interrupt is lost -- one dropped millisecond per
// occurrence, which shows up as a clock that runs slow under load and nowhere
// else.
// TIMER0 has ONE interrupt and now two things on it: this tick on MR2, and the
// PWM period on MR3 (see pwm_212x.c). The VIC has one vector per source, so the
// dispatch is here -- each side tests its own match flag and ignores the other.
//
// Weak, so a build without PWM links against an empty one rather than needing
// the file. Both are called on every TIMER0 interrupt.
void csp_pwm_timer0_isr(void) __attribute__((weak));
void csp_pwm_timer0_isr(void) { }

static void tick_wrapper(void)
{
    csp_pwm_timer0_isr();

    // Not `else`: both can be pending. Test the flag, because this handler runs
    // for the PWM match too and must do nothing then.
    if (!(LPC_TIMER0->IR & (1u << TICK_MR)))
	return;
    LPC_TIMER0->IR = (1u << TICK_MR);
    // RELATIVE to the match that just fired, not to TC: TC has moved on by
    // however long the interrupt took to be taken, and adding to it would let
    // that latency accumulate into the clock.
    LPC_TIMER0->MR[TICK_MR] += TICK_US;
    csp_tick_isr();
}

void Chip_Tick_Init(uint32_t hz)
{
    (void)hz;                           // 1 ms; the counter is fixed at 1 MHz

    Chip_TIMER_Init(LPC_TIMER0);
    // Prescale to 1 MHz so TC counts microseconds directly. PR holds the
    // divisor minus one, and PC counts up to it.
    LPC_TIMER0->PR = (pclk_rate / 1000000u) - 1u;
    LPC_TIMER0->TC = 0;
    LPC_TIMER0->MR[TICK_MR] = TICK_US;
    LPC_TIMER0->MCR = TICK_MR2I;        // interrupt, and NO reset of TC
    Chip_VIC_SetHandler(TIMER0_IRQn, tick_wrapper);
    NVIC_EnableIRQ(TIMER0_IRQn);
    Chip_TIMER_Enable(LPC_TIMER0);
}

// Microseconds, straight off the counter. No arithmetic and no critical
// section: a 32-bit read of TC is atomic, and it is already the answer.
//
// It wraps every 4295 seconds -- 71 minutes -- which the caller handles the
// same way it handles the Cortex-M version wrapping.
uint32_t Chip_Tick_Us(void)
{
    return LPC_TIMER0->TC;
}
