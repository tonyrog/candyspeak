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


// A SLOT IS A BYTE. It was not always: with NAMEID_BITS at 9 a slot straddled
// byte boundaries, six read-modify-writes of an unaligned 9-bit field came to
// 298 instructions in add_state, and a bit-at-a-time loop was smaller than the
// switch that replaced it. NAMEID_BITS is 8 now and CSP_STATES_BIT0 is 8, so
// slot k is byte 1+k at bit 0 -- the loop spends eight iterations building what
// one load already holds. The switch is back, as ONE function rather than
// expanded at every site, and it goes through the generated accessors so the
// layout stays the single description. tests/states_layout.c pins the
// regularity this depends on.
sindex_t csp_states_name(const csp_decl_t* d, int k)
{
    switch (k) {
    case 0: return (sindex_t)csp_decl_get_name(d);
    case 1: return (sindex_t)csp_decl_get_s6_name2(d);
    case 2: return (sindex_t)csp_decl_get_s6_name3(d);
    case 3: return (sindex_t)csp_decl_get_s6_name4(d);
    case 4: return (sindex_t)csp_decl_get_s6_name5(d);
    case 5: return (sindex_t)csp_decl_get_s6_name6(d);
    default: return 0;
    }
}

void csp_states_set_name(csp_decl_t* d, int k, sindex_t pos)
{
    switch (k) {
    case 0: csp_decl_set_name(d, (uint8_t)pos); break;
    case 1: csp_decl_set_s6_name2(d, (uint8_t)pos); break;
    case 2: csp_decl_set_s6_name3(d, (uint8_t)pos); break;
    case 3: csp_decl_set_s6_name4(d, (uint8_t)pos); break;
    case 4: csp_decl_set_s6_name5(d, (uint8_t)pos); break;
    case 5: csp_decl_set_s6_name6(d, (uint8_t)pos); break;
    default: break;
    }
}

