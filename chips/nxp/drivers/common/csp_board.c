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
