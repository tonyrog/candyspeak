// PWM on an LPC2000, both ways it is done.
//
// TWO MECHANISMS, because the pins are wired to two different peripherals:
//
//   PWM0      real hardware PWM. One period for the block, one match per
//             channel, the output flips in hardware. Nothing to service.
//
//   MATn.m    a timer match driving a pin. The hardware sets the pin on match
//             and that is all it does -- something has to put it back at the
//             start of the next period, which is an interrupt per period.
//
// The second is what pdb_pwm.c called soft PWM, and it exists because the
// LPC2129's single PWM block does not reach every pin a board wants to dim.
//
// THE POLARITY IS INVERTED, and deliberately. EMR is set to "high on match", so
// the period interrupt drives the pin LOW and the match drives it HIGH: the
// duty is the tail of the period, not the head. Doing it the other way needs
// the match to clear and the period to set, which is the same count of writes
// and one more thing to remember -- this way the match value IS the off-time.

#include <stdint.h>
#include "csp_config.h"
#include "chip_212x.h"
#include "vic_212x.h"

#define PWM_HZ 1000u                    // period, both mechanisms

// --- external match register bits -------------------------------------------
// EMR bits 0..3 are the current pin LEVELS -- writable, and writing them drives
// the pin. Bits 4+ are two-bit control fields saying what a match does. That
// two-registers-in-one is why the period handler writes the low bits and
// setup writes the high ones.
#define EMR_EM(n)      (1u << (n))
#define EMR_CTL_MASK(n) (3u << (4 + 2*(n)))
#define EMR_CTL_HIGH(n) (2u << (4 + 2*(n)))   // 2 = set high on match

// --- PWM0 -------------------------------------------------------------------
#define LPC_PWM0_BASE 0xE0014000u
#define PWM0_IR   (*(volatile uint32_t *)(LPC_PWM0_BASE + 0x00))
#define PWM0_TCR  (*(volatile uint32_t *)(LPC_PWM0_BASE + 0x04))
#define PWM0_TC   (*(volatile uint32_t *)(LPC_PWM0_BASE + 0x08))
#define PWM0_PR   (*(volatile uint32_t *)(LPC_PWM0_BASE + 0x0C))
#define PWM0_MCR  (*(volatile uint32_t *)(LPC_PWM0_BASE + 0x14))
// MR0..MR3 sit with the timer registers at 0x18..0x24; MR4..MR6 were added
// after them, at 0x40..0x48. The old macro put MR1 at 0x40, so MR(2) wrote MR5
// and MR(5) wrote LER at 0x50 -- Aout2 never came on (2026-10-08).
#define PWM0_MR(n) (*(volatile uint32_t *)(LPC_PWM0_BASE + \
		     ((n) <= 3 ? 0x18 + (n)*4 : 0x40 + ((n)-4)*4)))
#define PWM0_PCR  (*(volatile uint32_t *)(LPC_PWM0_BASE + 0x4C))
#define PWM0_LER  (*(volatile uint32_t *)(LPC_PWM0_BASE + 0x50))

#define PWM0_TCR_EN     (1u << 0)
#define PWM0_TCR_RESET  (1u << 1)
#define PWM0_TCR_PWMEN  (1u << 3)
#define PWM0_MCR_MR0R   (1u << 1)       // reset on MR0: MR0 is the period

static uint32_t pwm0_period;

// Timer channels: which match register each pin uses, and the period register.
// MR3 is the period on both timers, which is why a board cannot have four
// timer-driven channels per timer -- only three.
#define TMR_PERIOD_MR 3

static uint32_t t0_period, t1_period;

// The ON time of each match channel, in timer counts. Kept here and laid down
// by the period handler, period after period: a match register is an ABSOLUTE
// count on a timer that never resets, so a value written once is behind TC
// one period later and never matches again. Written by the output sweep only,
// that gave one pulse per CYCLE (~10 ms) instead of one per period (1 ms).
static volatile uint32_t t0_on[1];
static volatile uint32_t t1_on[3];

// The period handler for one timer: catch the period up, then for each of its
// `n` channels -- MAT0..MATn-1 -- start the period low and set the match for
// the new one. A channel fully off has no match at all (a match AT the period
// end would flash it for the interrupt latency), and one fully on is simply
// driven high.
static void tmr_period(LPC_TIMER_T *t, uint32_t period, volatile uint32_t *on,
		       int n)
{
    uint32_t end, emr;
    int m;

    if (!(t->IR & (1u << TMR_PERIOD_MR)))
	return;
    t->IR = (1u << TMR_PERIOD_MR);
    // RELATIVE to the match that fired, not to TC -- adding to TC would let
    // interrupt latency accumulate into the period -- and caught up past TC
    // whole periods at a time: neither timer resets its count, so a match left
    // behind TC would next fire when TC wraps, 71 minutes on. See the tick in
    // chip_212x.c, which fell into exactly this.
    do
	t->MR[TMR_PERIOD_MR] += period;
    while ((int32_t)(t->MR[TMR_PERIOD_MR] - t->TC) <= 0);
    end = t->MR[TMR_PERIOD_MR];

    emr = t->EMR;
    for (m = 0; m < n; m++) {
	emr &= ~(EMR_CTL_MASK(m) | EMR_EM(m));
	if (on[m] >= period)
	    emr |= EMR_EM(m);                   // fully on: high, no match
	else if (on[m] != 0) {
	    t->MR[m] = end - on[m];             // high from here to the end
	    emr |= EMR_CTL_HIGH(m);
	}
    }
    t->EMR = emr;
}

// TIMER0 is ALSO the system clock, and its tick lives on MR2 -- so this handler
// must not touch TC and must not assume it owns the interrupt. Both handlers
// are called for every TIMER0 interrupt; each tests its own flag.
void csp_pwm_timer0_isr(void) { tmr_period(LPC_TIMER0, t0_period, t0_on, 1); }
void csp_pwm_timer1_isr(void) { tmr_period(LPC_TIMER1, t1_period, t1_on, 3); }

void csp_pwm_init(void)
{
    uint32_t us = Chip_Clock_GetPeripheralClockRate() / 1000000u;

    // --- PWM0: prescale to 1 MHz, MR0 is the period and resets the count.
    PWM0_TCR = PWM0_TCR_RESET;
    PWM0_PR  = us - 1u;
    pwm0_period = 1000000u / PWM_HZ;
    PWM0_MR(0) = pwm0_period;
    PWM0_MCR = PWM0_MCR_MR0R;
    PWM0_LER = 0x7f;                    // latch every match register
    PWM0_TCR = PWM0_TCR_EN | PWM0_TCR_PWMEN;

    // --- TIMER1: free for PWM, so prescale it to 1 MHz here.
    Chip_TIMER_Init(LPC_TIMER1);
    LPC_TIMER1->PR = us - 1u;
    LPC_TIMER1->TC = 0;
    t1_period = 1000000u / PWM_HZ;
    LPC_TIMER1->MR[TMR_PERIOD_MR] = t1_period;
    LPC_TIMER1->MCR |= (1u << (3 * TMR_PERIOD_MR));      // interrupt on MR3
    LPC_TIMER1->EMR = 0;                // every channel off until written
    Chip_VIC_SetHandler(TIMER1_IRQn, csp_pwm_timer1_isr);
    NVIC_EnableIRQ(TIMER1_IRQn);
    Chip_TIMER_Enable(LPC_TIMER1);

    // --- TIMER0: already running as the system clock at 1 MHz, and NOT ours
    // to reset. Only add the period match and the output control.
    t0_period = 1000000u / PWM_HZ;
    LPC_TIMER0->MR[TMR_PERIOD_MR] = LPC_TIMER0->TC + t0_period;
    LPC_TIMER0->MCR |= (1u << (3 * TMR_PERIOD_MR));
}

// val is 0..255, as csp_board_analog_output scales it.
//
// Which mechanism a pin uses is decided HERE by the pin number, because that is
// where the board's wiring is already known -- boards/<name>.terms says P0.7 is
// pwm2 and P0.17 is mat1.2, and the generated header carries it.
void csp_lpc_pwm_write(uint8_t port, uint8_t pin, int val)
{
    uint32_t on;

    if (port != 0)
	return;
    if (val < 0) val = 0;
    if (val > 255) val = 255;

    switch (pin) {
    // PWM0 channels: the match is the ON time and the hardware does the rest.
    case 7:  on = (pwm0_period * (uint32_t)val) / 255u;   // PWM0.2
	PWM0_MR(2) = on; PWM0_PCR |= (1u << (8 + 2)); PWM0_LER |= (1u << 2);
	return;
    case 21: on = (pwm0_period * (uint32_t)val) / 255u;   // PWM0.5
	PWM0_MR(5) = on; PWM0_PCR |= (1u << (8 + 5)); PWM0_LER |= (1u << 5);
	return;

    // Timer matches: only the ON time is recorded here. The period handler
    // lays it down from the next period on -- see tmr_period.
    case 12: t1_on[0] = (t1_period * (uint32_t)val) / 255u; return; // MAT1.0
    case 13: t1_on[1] = (t1_period * (uint32_t)val) / 255u; return; // MAT1.1
    case 17: t1_on[2] = (t1_period * (uint32_t)val) / 255u; return; // MAT1.2
    case 22: t0_on[0] = (t0_period * (uint32_t)val) / 255u; return; // MAT0.0
    default:
	return;
    }
}
