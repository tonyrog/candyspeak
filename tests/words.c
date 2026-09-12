// One source, two back ends -- checked against each other.
//
// utils/words.terms describes is_local once. gen/csp_words.h is the C, and
// gen/csp_words_bc.h is the same word as micro-csp bytecode. This runs both
// over every index of the same declaration table and fails if they ever
// disagree, which is the only thing that makes "one description, two back
// ends" a fact instead of an intention.
//
// Nothing of the runtime is linked: csp_decl_ref and the two micro-csp hooks
// are defined here over a fake table, so both back ends see exactly the same
// state and the comparison is about the generators and nothing else.
//
// Built by `make words_check`. Prints "ok" and nothing else on success.

#include <stdio.h>
#include <string.h>
#include "csp.h"
#include "csp_layout.h"
#include "csp_mcsp.h"
#include "csp_words.h"
#include "csp_words_bc.h"

#define NDECL 8

static csp_decl_t table[NDECL];
static csp_rt_t   state;

// The runtime's own; not linked here, so both back ends reach the same array.
//
// Out of range returns a SPARE that looks like a local variable rather than
// NULL. Two reasons, both learned the hard way: a NULL makes the C back end
// dereference it and die where it should have reported a mismatch, and a spare
// that answers "yes" is what makes the word's bounds test discriminating --
// with a zeroed spare, `i < nd` and `i <= nd` give the same answer and the
// check proves nothing.
static csp_decl_t spare;

const csp_decl_t* csp_decl_ref(csp_rt_t* st, index_t i)
{
    (void)st;
    return (i < NDECL) ? &table[i] : &spare;
}

// A native the word calls. Stubbed for the same reason csp_decl_ref is: this
// test is about the two back ends agreeing, and they agree by calling the SAME
// function -- what the real csp_tag returns is the repl suite's business.
const char csp_tag(csp_rt_t* st, index_t n)
{
    (void)st;
    return (char)('A' + (csp_decl_get_type(csp_decl_ref(NULL, n)) & 7));
}

static const void* hook_decl(void* ctx, mc_cell_t i)
{
    return csp_decl_ref((csp_rt_t*)ctx, (index_t)i);
}

static mc_cell_t hook_nd(void* ctx)
{
    return (mc_cell_t)((csp_rt_t*)ctx)->ps.nd;
}

// The ORIGINAL, break and all, copied from src/csp_print.c before it was
// written as a word. local_number had to be restructured to lose the break --
// the IR has no second exit from a loop -- and "restructured" is exactly the
// kind of claim that needs checking rather than believing. Both generated back
// ends are compared against this, not only against each other.
static int ref_local_number(csp_rt_t* st, index_t ix)
{
    index_t i = INDEX(ix);
    index_t start = 0;
    index_t k;
    int n = 0;

    for (k = i; k > 0; k--) {
	decl_t t = decl(st, k-1, type);
	if ((t == DECL_MODULE) || (t == DECL_END)) {
	    start = k;
	    break;
	}
    }
    for (k = start; k < i; k++) {
	if ((decl(st, k, type) == DECL_VARIABLE) && decl(st, k, local))
	    n++;
    }
    return n + 1;
}

static int errors = 0;

static mc_cell_t ds[16];
static uint16_t  rs[8];
static mc_cell_t lv[8];

static int run_bc(uint16_t entry, const mc_cell_t arg, mc_cell_t* out)
{
    mc_vm_t vm;

    memset(&vm, 0, sizeof(vm));
    vm.code = csp_words_bc;
    vm.code_len = (uint16_t)sizeof(csp_words_bc);
    vm.ds = ds;      vm.ds_size = (uint8_t)(sizeof(ds)/sizeof(ds[0]));
    vm.rs = rs;      vm.rs_size = (uint8_t)(sizeof(rs)/sizeof(rs[0]));
    vm.lv = lv;      vm.lv_size = (uint8_t)(sizeof(lv)/sizeof(lv[0]));
    // The GENERATED tables: a word's natives are whatever words.terms declared,
    // not something this test picks.
    vm.leaf = csp_word_leaves;
    vm.nleaf = csp_word_leaves_N;
    vm.leafn = csp_word_leavesn;
    vm.nleafn = csp_word_leavesn_N;
    vm.ctx = &state;
    // On the DATA STACK, where a word's caller leaves them -- the entry word
    // is not a special case.
    memset(lv, 0, sizeof(lv));
    vm.arg = &arg;
    vm.nargs = 1;
    return csp_mcsp_run(&vm, entry, out);
}

int main(void)
{
    int i, k;
    // DECL_MODULE and DECL_END are in here on purpose: they are what the
    // downward scan in local_number stops on, and without one in the table the
    // loop never matches and the break it was restructured to lose is never
    // exercised at all.
    static const decl_t kinds[NDECL] = {
	DECL_VARIABLE, DECL_MODULE,   DECL_VARIABLE, DECL_CONSTANT,
	DECL_END,      DECL_VARIABLE, DECL_VARIABLE, DECL_TIMER
    };
    static const uint8_t locals[NDECL] = { 0, 0, 1, 1, 0, 1, 1, 0 };

    mc_decl_hook = hook_decl;
    mc_nd_hook = hook_nd;

    memset(&spare, 0, sizeof(spare));
    csp_decl_set_type(&spare, (uint8_t)DECL_VARIABLE);
    csp_decl_set_local(&spare, 1);

    memset(&state, 0, sizeof(state));
    for (i = 0; i < NDECL; i++) {
	memset(&table[i], 0, sizeof(table[i]));
	csp_decl_set_type(&table[i], (uint8_t)kinds[i]);
	csp_decl_set_local(&table[i], locals[i]);
    }

    // Every count, so the bounds test in the word is exercised from both
    // sides, and one index past the end of the table for good measure.
    for (k = 0; k <= NDECL; k++) {
	state.ps.nd = (index_t)k;
	for (i = 0; i <= NDECL; i++) {
	    int c = csp_is_local(&state, (index_t)i);
	    mc_cell_t b = 0;
	    int rc = run_bc(CSP_W_IS_LOCAL_ENTRY, (mc_cell_t)i, &b);

	    if (rc != MC_OK) {
		printf("FAIL nd=%d ix=%d: bytecode stopped, rc=%d\n", k, i, rc);
		errors++;
		continue;
	    }
	    if ((c != 0) != (b != 0)) {
		printf("FAIL is_local nd=%d ix=%d: C says %d, bytecode says %u\n",
		       k, i, c, (unsigned)b);
		errors++;
	    }

	    c = csp_local_number(&state, (index_t)i);
	    rc = run_bc(CSP_W_LOCAL_NUMBER_ENTRY, (mc_cell_t)i, &b);
	    if (rc != MC_OK) {
		printf("FAIL local_number nd=%d ix=%d: bytecode stopped, rc=%d\n",
		       k, i, rc);
		errors++;
	    } else if (c != (int)b) {
		printf("FAIL local_number nd=%d ix=%d: C says %d, bytecode says %u\n",
		       k, i, c, (unsigned)b);
		errors++;
	    }
	    // switch, both back ends. The C gets a real switch and the bytecode a
	    // compare chain, so this is the one place the two structures differ
	    // most -- which is why it is checked rather than assumed.
	    c = csp_decl_kind(&state, (index_t)i);
	    rc = run_bc(CSP_W_DECL_KIND_ENTRY, (mc_cell_t)i, &b);
	    if (rc != MC_OK) {
		printf("FAIL decl_kind ix=%d: bytecode stopped, rc=%d\n", i, rc);
		errors++;
	    } else if (c != (int)b) {
		printf("FAIL decl_kind ix=%d: C says %d, bytecode says %u\n",
		       i, c, (unsigned)b);
		errors++;
	    }

	    c = csp_local_number(&state, (index_t)i);
	    // leaf_mark calls TWO ways: is_local is a word, so MC_CALL into the
	    // shared area with a frame; csp_tag is C, so a wrapper. Neither is
	    // spelled differently in words.terms.
	    c = csp_leaf_mark(&state, (index_t)i);
	    rc = run_bc(CSP_W_LEAF_MARK_ENTRY, (mc_cell_t)i, &b);
	    if (rc != MC_OK) {
		printf("FAIL leaf_mark nd=%d ix=%d: bytecode stopped, rc=%d\n",
		       k, i, rc);
		errors++;
	    } else if (c != (int)b) {
		printf("FAIL leaf_mark nd=%d ix=%d: C says %d, bytecode says %u\n",
		       k, i, c, (unsigned)b);
		errors++;
	    }

	    c = csp_local_number(&state, (index_t)i);
	    if (c != ref_local_number(&state, (index_t)i)) {
		printf("FAIL local_number nd=%d ix=%d: word says %d, the original %d\n",
		       k, i, c, ref_local_number(&state, (index_t)i));
		errors++;
	    }
	}
    }
    if (errors == 0)
	printf("ok\n");
    return (errors == 0) ? 0 : 1;
}
