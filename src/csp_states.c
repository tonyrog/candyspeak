// The state-name packing, and nothing else.
//
// A DECL_STATES block holds CSP_STATES_PER_DECL names in one 8-byte
// declaration: slot 0 is DECL_COMMON's `name` (the alias everything else reads)
// and the rest follow at NAMEID_BITS each. Slot k therefore starts at bit
// CSP_STATES_BIT0 + k*NAMEID_BITS -- regular, and tests/states_layout.c is what
// holds the struct to it.
//
// ITS OWN FILE because it depends on nothing but the types. That is what lets
// tests/states_layout.c link the REAL accessors instead of restating them, and
// a layout test that tests its own copy of the layout tests nothing.

#include <stdint.h>
#include <stddef.h>
#include "csp.h"
#include "csp_words.h"
// A run of `n` bits at bit offset `bit`, LSB first, out of the declaration read
// as bytes. Deliberately a LOOP and not six constant shifts: this is a size
// problem, not a speed one, and it runs once per state at boot.
//
// See csp.h for the numbers -- the six-armed inline version cost 202 bytes to
// read and put ~300 into add_state to write, on an 8-bit machine with a 1-bit
// shifter and 9-bit fields that straddle byte boundaries.


