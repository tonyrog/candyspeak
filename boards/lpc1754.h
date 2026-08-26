// LPC1754FBD80 -- 100 MHz Cortex-M3, 128K flash, 16K main SRAM.
//
// A PLAIN firmware: no boot loader, the whole flash is ours, and the vector
// table sits at 0x00000000 where the core looks for it. Flashed over ISP or
// JTAG. (boards/dl1200.h is the same part with a USB boot loader in the low
// sectors, which is what forces that board's map to start at sector 4.)
//
// FLASH (chips/nxp/175x.terms is the source; `csp --ld=lpc1754`):
//
//   0..16   runtime   96K   measured at 67112, so 29K spare
//   17      store     32K   settings and patches, together
//
// Together because there is nowhere else. The vector table pins the runtime to
// sector 0, 67K does not fit in the 64K of small sectors, so the runtime has
// to reach into a 32K one -- and what is left is a single sector. On a node
// with a compiler that is the right trade anyway: the program arrives as
// source, so there is nothing an application slot would hold.
#include "embedded.h"

#define CSP_CHIP      lpc1754

// 12 MHz crystal -> 100 MHz core: M=100, N=6, div=4 puts Fcco at 400 MHz.
// Same PLL0CFG = 0x00050063 that boards/dl1200/system.c arrives at.
#define CSP_XTAL_HZ   12000000
#define CSP_CORE_HZ   100000000

// The arena. CSP_CODE_BUDGET and NOT CSP_ARENA_BYTES -- the latter is derived
// from it in csp.h and is not #ifndef-guarded, so setting it here does nothing.
//
// It has to be said explicitly: csp.h picks by target, its branches are AVR,
// ARDUINO and "host: full worst case", and a bare-metal ARM that is not an
// Arduino lands in the last one asking for 24K -- more than this part's 16K of
// main SRAM. The link then fails with "region DATA overflowed", which is how
// this was found. Measured: 12420 bytes of bss with the budget at 9216.
#define CSP_CODE_BUDGET 9216

// No EEPROM peripheral on a 175x. Its 177x/8x sibling has one and
// eeprom_17xx_40xx.c sits in the same driver directory, which makes it look
// otherwise -- but chip_lpc175x_6x.h never defines LPC_EEPROM. Persistence
// goes to the flash `store` region above.
#define CSP_NO_EEPROM 1

#define CSP_LPC_BAUD  115200
