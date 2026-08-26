// What startup calls before main: clock, memory accelerator, interrupt
// controller. The CMSIS name, so the assembly says `bl SystemInit` on either
// family.
//
// The three numbers come from the BOARD. A crystal is soldered to a board, not
// designed into a chip, and the core clock is a choice somebody made -- so they
// arrive as defines and this file does not guess.

// csp_config.h is what includes the board file named by CSP_BOARD, and the
// board is where the crystal is stated. It needs the fixed-width types first --
// it declares typedefs over them and does not include stdint itself.
#include <stdint.h>
#include "csp_config.h"
#include "chip_212x.h"
#include "vic_212x.h"

#ifndef CSP_XTAL_HZ
#error "the board must define CSP_XTAL_HZ (the crystal, in Hz)"
#endif
#ifndef CSP_CORE_HZ
#define CSP_CORE_HZ 60000000        // the LPC2129 ceiling
#endif
#ifndef CSP_PCLK_DIV
#define CSP_PCLK_DIV 4              // VPBDIV reset value: cclk/4
#endif

void csp_board_pinmux(void);

void SystemInit(void)
{
    Chip_SystemInit(CSP_XTAL_HZ, CSP_CORE_HZ, CSP_PCLK_DIV);
    SystemCoreClockUpdate();

    // Power and pin functions, from boards/<name>.terms. AFTER the clock,
    // because writing PCONP needs the peripheral bus running, and BEFORE any
    // driver, because a driver that initialises an unpowered block writes into
    // nothing and reports success.
    csp_board_pinmux();

    // Unmask interrupts. LAST, and it has to be here because nothing else does
    // it: a Cortex-M boots with interrupts enabled, so a platform file written
    // against one never asks. An ARM7 boots masked -- startup_212x.S leaves
    // I and F set -- and the first csp_delay_ms then spins in __WFI() forever
    // waiting for a tick that cannot arrive.
    //
    // Safe here: Chip_VIC_Init has cleared every source enable, so nothing can
    // fire until a driver asks for it.
    EnableIRQ();
    // Before any driver registers a handler, and before main enables
    // interrupts: a VIC left in its reset state has every slot pointing at
    // address 0, so one stray source would execute the reset vector.
    Chip_VIC_Init();
}
