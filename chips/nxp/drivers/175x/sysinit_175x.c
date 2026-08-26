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

void SystemInit(void)
{
    // Main oscillator on and stable before anything selects it. Selecting an
    // oscillator that has not started leaves the part running on the IRC at
    // 4 MHz -- everything works, at a twenty-fifth of the speed.
    Chip_Clock_SetMainPLLSource(SYSCTL_PLLCLKSRC_MAINOSC);
    Chip_Clock_EnableCrystal();
    while (!Chip_Clock_IsCrystalEnabled())
	;

    Chip_Clock_SetCPUClockDiv(CCLK_DIV - 1);
    Chip_Clock_SetupPLL(SYSCTL_MAIN_PLL, PLL0_M - 1, PLL0_N - 1);
    Chip_Clock_EnablePLL(SYSCTL_MAIN_PLL, SYSCTL_PLL_ENABLE);
    while (!(Chip_Clock_GetPLLStatus(SYSCTL_MAIN_PLL) & SYSCTL_PLL0STS_LOCKED))
	;
    Chip_Clock_EnablePLL(SYSCTL_MAIN_PLL,
			 SYSCTL_PLL_ENABLE | SYSCTL_PLL_CONNECT);

    SystemCoreClockUpdate();

    // Power and pin functions, from boards/<name>.terms. AFTER the clock,
    // because writing PCONP needs the peripheral bus running, and BEFORE any
    // driver, because a driver that initialises an unpowered block writes into
    // nothing and reports success.
    //
    // This matters more here than on an ARM7: LPCOpen does not touch PINSEL,
    // so an unmuxed TXD0 is a GPIO input and the console is silent while the
    // UART runs perfectly.
    csp_board_pinmux();
}
