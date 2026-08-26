// SystemInit for LPC175x: the clock, through LPCOpen.
//
// The board says what crystal it has and what core clock it wants; the numbers
// below are what those two imply, solved once and written down. The plan is
// for gen_chips.erl to do the solving -- utils/lpc_clock.varp already states
// the equations -- so an unreachable target fails the build.

// csp_config.h is what includes the board file named by CSP_BOARD, and the
// board is where the crystal is stated. It needs the fixed-width types first
// -- it declares typedefs over them and does not include stdint itself.
#include <stdint.h>
#include "csp_config.h"
#include "chip.h"

#ifndef CSP_XTAL_HZ
#error "the board must define CSP_XTAL_HZ"
#endif
#ifndef CSP_CORE_HZ
#define CSP_CORE_HZ 100000000
#endif

// Fcco = 2 * M * Fin / N, then divided down to cclk.
//
// For the 12 MHz crystal on a DL1200: M = 100, N = 6, div = 4.
//   Fcco = 2 * 100 * 12e6 / 6 = 400 MHz   (must be 275..550)
//   cclk = 400 / 4             = 100 MHz  (the part's ceiling)
//
// The registers hold M-1 and N-1, which is why PLL0CFG reads 0x00050063:
// MSEL = 0x63 = 99, NSEL = 0x05 = 5. Same value boards/dl1200/system.c has,
// arrived at the same way.
#define PLL0_M   100
#define PLL0_N   6
#define CCLK_DIV 4

// LPCOpen asks the BOARD for its oscillators, as variables rather than defines:
// Chip_Clock_GetSYSCLKRate reads them at run time, so a library built once
// serves any crystal. They have to exist somewhere, and the board is the only
// thing that knows -- which is the same argument as CSP_XTAL_HZ, arrived at
// from the library's side.
const uint32_t OscRateIn    = CSP_XTAL_HZ;
const uint32_t RTCOscRateIn = 32768;

void csp_board_pinmux(void);
extern void (* const g_vectors[])(void);

// A bounded wait. An unbounded one is how a board that will not start a crystal
// becomes a board that does nothing at all, forever, with no way to tell that
// from a dozen other faults. Falling through on a timeout at least leaves
// something running that can be asked what happened.
//
// The count is cycles of a `while` at up to 100 MHz, so this is milliseconds,
// not microseconds -- far longer than the ~300 us a 12 MHz crystal needs.
#define WAIT_LIMIT 1000000u

void SystemInit(void)
{
    uint32_t n;

    // The vector table. At address 0 the reset value of VTOR is already 0 and
    // this changes nothing -- but it stops being true the moment the image
    // moves, which on this board is one line in boards/dl1200.terms.
    SCB->VTOR = (uint32_t)&g_vectors;

    // ORDER MATTERS HERE, and getting it wrong is silent.
    //
    // The oscillator is started and waited for BEFORE anything selects it.
    // Writing CLKSRCSEL to choose an oscillator that is not running takes the
    // clock away from the core -- the part simply stops, with no fault and no
    // output, and looks exactly like a board that was never programmed. The
    // user manual warns about this register in those words.
    //
    // The previous version here did it the other way round.
    Chip_Clock_EnableCrystal();
    for (n = WAIT_LIMIT; n && !Chip_Clock_IsCrystalEnabled(); n--)
	;

    // The CPU divider before the PLL is connected, so cclk is in range the
    // instant it is.
    Chip_Clock_SetCPUClockDiv(CCLK_DIV - 1);

    // NOW it is safe to select it.
    Chip_Clock_SetMainPLLSource(SYSCTL_PLLCLKSRC_MAINOSC);

    // SetupPLL writes PLLCFG, sets PLLCON = 1 and does the feed sequence, so
    // the enable below is a second feed rather than the first enable. The feed
    // is not optional: without the 0xAA/0x55 pair the write is ignored, no
    // fault, and the part keeps running on the crystal at a fifth of the speed.
    Chip_Clock_SetupPLL(SYSCTL_MAIN_PLL, PLL0_M - 1, PLL0_N - 1);
    Chip_Clock_EnablePLL(SYSCTL_MAIN_PLL, SYSCTL_PLL_ENABLE);

    // PLOCK is bit 26 on PLL0 -- bit 10 is PLL1's, and SYSCTL_PLLSTS_LOCKED is
    // the generic name for that one. Waiting on the wrong bit either never
    // finishes or finishes immediately, and both are quiet.
    for (n = WAIT_LIMIT; n; n--)
	if (Chip_Clock_GetPLLStatus(SYSCTL_MAIN_PLL) & SYSCTL_PLL0STS_LOCKED)
	    break;

    if (n)
	Chip_Clock_EnablePLL(SYSCTL_MAIN_PLL,
			     SYSCTL_PLL_ENABLE | SYSCTL_PLL_CONNECT);

    // Whatever actually happened above, this reads the registers back -- so
    // SystemCoreClock is the truth and not the intention, and the UART divisor
    // is computed from the clock the part really has.
    SystemCoreClockUpdate();

#if defined(CSP_BOOT_LED_PORT)
    // Blink, here, before the runtime exists. This is the one signal that does
    // not need the UART, a correct baud rate, or a terminal on the other end --
    // so it separates "the part is not running" from "the part is running and
    // the console is wrong", which is otherwise one symptom with two causes.
    //
    // Deliberately BEFORE csp_board_pinmux: the LED is a GPIO at reset, so it
    // needs no mux, and putting it first means it also reports a fault in the
    // mux itself.
    {
	int i;
	volatile uint32_t d;
	Chip_Clock_EnablePeriphClock(SYSCTL_CLOCK_GPIO);
	Chip_GPIO_SetPinDIROutput(LPC_GPIO, CSP_BOOT_LED_PORT, CSP_BOOT_LED_PIN);
	// Three blinks. A count, not a steady state: a LED that is simply on
	// could be a stuck pin, and one that is off could be anything at all.
	for (i = 0; i < 6; i++) {
	    // XOR with the polarity, so `on` means lit on either wiring.
	    Chip_GPIO_SetPinState(LPC_GPIO, CSP_BOOT_LED_PORT,
				  CSP_BOOT_LED_PIN,
				  ((i & 1) == 0) ? CSP_BOOT_LED_ON
						 : !CSP_BOOT_LED_ON);
	    // A spin, because there is no tick yet. Roughly 100 ms at 100 MHz,
	    // and if the PLL did not connect it will visibly be 25 times
	    // slower -- which makes the blink RATE a clock diagnostic too.
	    for (d = 0; d < 2500000u; d++)
		;
	}
	// Leave it OFF, which on this board means driving the pin HIGH. Ending
	// on the wrong level is what made the first version look like a solid
	// lamp: the blink happened and then it parked itself lit.
	Chip_GPIO_SetPinState(LPC_GPIO, CSP_BOOT_LED_PORT, CSP_BOOT_LED_PIN,
			      !CSP_BOOT_LED_ON);
    }
#endif

    csp_board_pinmux();
}
