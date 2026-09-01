// The contract behind csp_states_t.
//
// A states block packs CSP_STATES_PER_DECL names into one csp_decl_t, and the
// design leans on one structural assumption that nothing would fail to compile
// over: slot 0 has to sit at exactly the same bits as DECL_COMMON's `name`.
// That alias is what lets everything reading a plain `d.name` see the block's
// first state without special-casing it -- lookup_decl_in, the listing, the ROM
// dump. Break the alignment (reorder csp_states_t, widen `dir`, change
// NAMEID_BITS) and lookups start silently missing the first state of every
// block while the other five keep working.
//
// So this PROBES the real struct rather than restating the numbers: write a
// distinct value into each slot, read the whole decl back through the other
// union arms, and check that the slots are independent, that slot 0 aliases
// DECL_COMMON.name, and that the block still fits a declaration.
//
// Run from tests/repl.sh. Prints "ok, identical" on success.

#include <stdio.h>
#include <string.h>
#include "csp.h"

static int errors = 0;

static void fail(const char* what, unsigned long got, unsigned long want)
{
    printf("MISMATCH %s: got %lu, want %lu\n", what, got, want);
    errors++;
}

// The largest name position the field can hold, so the probe uses values that
// actually exercise the full width instead of small ones that would fit
// anywhere.
#define NP_MAX (MAX_NAMEIDS - 1u)

int main(void)
{
    csp_decl_t d;
    unsigned v[CSP_STATES_PER_DECL];
    int k;

    // A block must not be bigger than the declaration it lives in.
    if (sizeof(csp_states_t) > sizeof(csp_decl_t))
	fail("sizeof(csp_states_t)",
	     (unsigned long)sizeof(csp_states_t),
	     (unsigned long)sizeof(csp_decl_t));

    // Distinct, full-width values so a slot that overlaps another shows up.
    for (k = 0; k < CSP_STATES_PER_DECL; k++)
	v[k] = NP_MAX - (unsigned)k;

    memset(&d, 0, sizeof(d));
    d.type      = DECL_STATES;
    d.s6.name   = v[0];
    d.s6.name2  = v[1];
    d.s6.name3  = v[2];
    d.s6.name4  = v[3];
    d.s6.name5  = v[4];
    d.s6.name6  = v[5];

    // Every slot reads back what was written: no two share bits.
    for (k = 0; k < CSP_STATES_PER_DECL; k++) {
	unsigned got = (unsigned)csp_states_name(&d, k);
	if (got != v[k]) {
	    char buf[32];
	    sprintf(buf, "slot %d", k);
	    fail(buf, got, v[k]);
	}
    }

    // The type survives the packing -- it shares the word with the names.
    if (d.type != DECL_STATES)
	fail("type", (unsigned long)d.type, (unsigned long)DECL_STATES);

    // THE alias: slot 0 is DECL_COMMON's name, read through the anonymous arm.
    if (d.name != v[0])
	fail("slot 0 aliases DECL_COMMON.name", d.name, v[0]);

    // And the reverse -- writing through DECL_COMMON lands in slot 0.
    d.name = 1;
    if (csp_states_name(&d, 0) != 1)
	fail("DECL_COMMON.name writes slot 0",
	     (unsigned long)csp_states_name(&d, 0), 1UL);

    // An empty slot reads 0, which is what marks padding at the end of a block.
    memset(&d, 0, sizeof(d));
    d.type = DECL_STATES;
    for (k = 0; k < CSP_STATES_PER_DECL; k++) {
	if (csp_states_name(&d, k) != 0) {
	    char buf[32];
	    sprintf(buf, "empty slot %d", k);
	    fail(buf, (unsigned long)csp_states_name(&d, k), 0UL);
	}
    }

    // Out of range is 0 too, so a loop that overruns cannot read a stale name.
    if (csp_states_name(&d, CSP_STATES_PER_DECL) != 0)
	fail("slot past the end",
	     (unsigned long)csp_states_name(&d, CSP_STATES_PER_DECL), 0UL);

    // Which DECL_COMMON fields OVERLAP a name, and which do not.
    //
    // This is the contract that has cost three bugs so far, all of the same
    // shape and none of them a compile error: `State = c` corrupting a name
    // through map_reg, the ROM generator zeroing scratch bits before the CRC,
    // and csp_new_decl inheriting a stale `bound` from a recycled slot. Any new
    // field in DECL_COMMON silently takes bits from name2 or name3 -- so pin
    // down exactly which fields do that, and which are shared with the block.
    //
    // Shared prefix: writing these must NOT disturb any slot.
    {
	static const char* shared[] = { "type", "dir" };
	int w;
	for (w = 0; w < 2; w++) {
	    int q, hit = 0;
	    memset(&d, 0, sizeof(d));
	    if (w == 0) d.type = (decl_t)0xf; else d.dir = (pindir_t)3;
	    for (q = 0; q < CSP_STATES_PER_DECL; q++)
		if (csp_states_name(&d, q) != 0) hit = 1;
	    if (hit) {
		char buf[64];
		sprintf(buf, "DECL_COMMON.%s must not touch a slot", shared[w]);
		fail(buf, 1, 0);
	    }
	}
    }
    // Overlapping fields: each must land in the slot named here and nowhere
    // else. If one of these moves, the code that special-cases DECL_STATES
    // (csp_dump_code's CRC fold, csp_new_decl's clear) is looking at the wrong
    // bits and has to move with it.
    {
	struct { const char* name; int slot; } over[] = {
	    { "vt",        1 },
	    { "res",       1 },
	    { "is_mapped", 2 },
	    { "bound",     2 },
	    { "reg",       2 },
	};
	int w;
	for (w = 0; w < 5; w++) {
	    int q;
	    memset(&d, 0, sizeof(d));
	    switch (w) {
	    case 0: d.vt = (vtype_t)0xf; break;
	    case 1: d.res = 0x1f; break;
	    case 2: d.is_mapped = 1; break;
	    case 3: d.bound = 1; break;
	    case 4: d.reg = 0xf; break;
	    }
	    for (q = 0; q < CSP_STATES_PER_DECL; q++) {
		int touched = (csp_states_name(&d, q) != 0);
		if (touched != (q == over[w].slot)) {
		    char buf[80];
		    sprintf(buf, "DECL_COMMON.%s vs slot %d", over[w].name, q);
		    fail(buf, (unsigned long)touched, (unsigned long)(q == over[w].slot));
		}
	    }
	}
    }

    if (errors == 0)
	printf("ok, identical\n");
    return errors ? 1 : 0;
}
