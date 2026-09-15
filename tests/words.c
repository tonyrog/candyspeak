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
#include "csp_print.h"
#include "csp_words.h"
#include "csp_words_c.h"   // the C back end, compared against the bytecode below
#include "csp_words_bc.h"
#include "csp_layout_raw.h"

#define NDECL  8
#define NINSTR 8
#define NBUF   4
#define NVIEW  9

static csp_decl_t  table[NDECL];
static csp_instr_t instr[NINSTR];
static csp_buf_t   bufs[NBUF];
static csp_view_t  views[NVIEW];
static csp_rt_t    state;

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


// The console, recorded. list_pin_spec is the first word whose whole point is
// what it PRINTS -- the return value says nothing -- so the two back ends have
// to be compared on the text. These are the `pure` natives: no csp_rt_t, which
// is what makes them pure and what the generated wrapper has to get right.
static char  pr_buf[256];
static unsigned pr_n;

int csp_print_char(char c)
{
    if (pr_n + 1 < sizeof(pr_buf))
	pr_buf[pr_n++] = c;
    pr_buf[pr_n] = 0;
    return 1;
}

int csp_print_uint(uvalue_t v)
{
    char t[12];
    int k = 0;
    if (v == 0) return csp_print_char('0');
    while (v && k < (int)sizeof(t)) { t[k++] = (char)('0' + (v % 10)); v /= 10; }
    while (k--) csp_print_char(t[k]);
    return 1;
}

static const void* hook_decl(void* ctx, mc_cell_t i)
{
    return csp_decl_ref((csp_rt_t*)ctx, (index_t)i);
}

static const void* hook_instr(void* ctx, mc_cell_t i)
{
    (void)ctx;
    return (i < NINSTR) ? &instr[i] : NULL;
}

// The C back end reads an instruction through instr(st,n,fld), which is
// csp_load_instr -- and the runtime is not linked here. Same table the hook
// hands the bytecode, so the two back ends are looking at the same bytes.
void csp_load_instr(csp_rt_t* st, index_t n, csp_instr_t* dst)
{
    (void)st;
    memcpy(dst, &instr[(n < NINSTR) ? n : 0], sizeof(csp_instr_t));
}


static const void* hook_buf(void* ctx, mc_cell_t i)
{
    return &bufs[i];
}

static const void* hook_view(void* ctx, mc_cell_t i)
{
    return &views[i];
}

// The WRITE hooks. Here they reach the same tables the reads do -- there is no
// ROM and no cache in this file -- but they are separate pointers, because in
// the runtime they are separate and a test that shared them would not be
// running the same shape of thing.
static void* wr_buf(void* ctx, mc_cell_t i)
{
    (void)ctx;
    return (i < NBUF) ? &bufs[i] : NULL;
}

static void* wr_view(void* ctx, mc_cell_t i)
{
    (void)ctx;
    return (i < NVIEW) ? &views[i] : NULL;
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
// Sixteen: list_pin_spec has seven locals on top of its two arguments, and
// a called word's frame sits ABOVE its caller's.
static mc_cell_t lv[24];

static int run_bcn(uint16_t entry, const mc_cell_t* args, uint8_t n,
		   mc_cell_t* out)
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
    vm.arg = args;
    vm.nargs = n;
    return csp_mcsp_run(&vm, entry, out);
}

static int run_bc(uint16_t entry, const mc_cell_t arg, mc_cell_t* out)
{
    return run_bcn(entry, &arg, 1, out);
}

// The buffer table, rebuilt from scratch. A word that WRITES has to be run
// against the same starting point twice -- once as C, once as bytecode -- or
// the second run is reading what the first one left.
static void setup_bufs(void)
{
    int i;

    memset(bufs, 0, sizeof(bufs));
    for (i = 0; i < NBUF; i++) {
	csp_buf_set_owner(&bufs[i], (index_t)(i + 1));
	csp_buf_set_transport(&bufs[i], (uint8_t)(i + 1));
	// dir shares byte 4 with transport and flags sits in the next one, and
	// both start NONZERO: a store that fails to clear the old bits, or
	// that writes a byte the field does not reach, needs something there
	// to destroy before it can be seen destroying it.
	csp_buf_set_dir(&bufs[i], 0x0D);
	csp_buf_set_flags(&bufs[i], 0xC3);
	// One buffer carries the endpoint the word looks for. Another carries
	// one with the SAME LOW HALF and a different high -- without it the
	// compare passes while looking at sixteen bits, and a double that only
	// checks half of itself is exactly the bug this is here to catch.
	csp_buf_set_xref(&bufs[i],
			 (i == 2) ? 0xDEADBEEFUL :
			 (i == 3) ? 0x1234BEEFUL : (uint32_t)i);
    }
    state.buf = bufs;
    state.nbuf = NBUF;
    // A view per leaf, each pointing at a different buffer, so leaf_buf has
    // something to be wrong about. cbase stays 0: the OBJECT context is what
    // st_index adds, and a word that reached the wrong view would show up here
    // as the wrong buffer id.
    memset(views, 0, sizeof(views));
    for (i = 0; i < NVIEW; i++)
	csp_view_set_buf(&views[i], (index_t)(NVIEW - i));
    state.view = views;
    state.view_cap = NVIEW;
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
    // Continuations, i.e. array elements: decl 5 is a head with 6 behind it,
    // so array_len has a run to count and one that stops at the table end.
    static const uint8_t conts[NDECL] = { 0, 0, 0, 0, 0, 0, 1, 1 };

    printf("INFO sizeof(csp_decl_t) = %ld\n", sizeof(csp_decl_t));
    printf("INFO sizeof(csp_buf_t) = %ld\n", sizeof(csp_buf_t));
    printf("INFO sizeof(csp_view_t) = %ld\n", sizeof(csp_view_t));    
    printf("INFO sizeof(table) = %ld = %d*%ld\n",
	   sizeof(table), NDECL, sizeof(table[0]));
    printf("INFO sizeof(bufs) = %ld = %d*%ld\n",
	   sizeof(bufs), NBUF, sizeof(bufs[0]));
    printf("INFO sizeof(views) = %ld = %d*%ld\n",
	   sizeof(views), NVIEW, sizeof(views[0]));

    printf("INFO sizeof(csp_decl_raw_t) = %ld\n", sizeof(csp_decl_raw_t));
    printf("INFO sizeof(csp_buf_raw_t) = %ld\n", sizeof(csp_buf_raw_t));
    printf("INFO sizeof(csp_view_raw_t) = %ld\n", sizeof(csp_view_raw_t));

    mc_decl_hook = hook_decl;
    mc_instr_hook = hook_instr;
    mc_view_hook = hook_view;
    mc_buf_hook = hook_buf;
    mc_state_hook = csp_word_state;   // generated; see utils/words.terms
    mc_buf_wr     = wr_buf;
    mc_view_wr    = wr_view;
    mc_state_set  = csp_word_state_set;

    memset(&spare, 0, sizeof(spare));
    csp_decl_set_type(&spare, (uint8_t)DECL_VARIABLE);
    csp_decl_set_local(&spare, 1);

    memset(&state, 0, sizeof(state));
    // A buffer table, so a word can reach one. st->buf is a pointer into the
    // arena in a real runtime; here it is a static, which is all the word can
    // tell apart.
    setup_bufs();
    for (i = 0; i < NDECL; i++) {
	memset(&table[i], 0, sizeof(table[i]));
	csp_decl_set_type(&table[i], (uint8_t)kinds[i]);
	csp_decl_set_local(&table[i], locals[i]);
	csp_decl_set_cont(&table[i], conts[i]);
    }
    // A gate: two OP_NINSTATE, then an OP_INSTATE, then something else. Both
    // the run that is skipped and the answer after it are in here, and so is a
    // range that ends inside the run -- which is the case that says `to`
    // is honoured and not just the opcode.
    for (i = 0; i < NINSTR; i++) {
	memset(&instr[i], 0, sizeof(instr[i]));
	csp_instr_set_op(&instr[i],
			 (i < 2) ? OP_NINSTATE : (i == 2) ? OP_INSTATE : OP_ADD);
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

	    // The two newest. Both run to a BOUND -- nd for one, `to` for the
	    // other -- and a bound is what a back end gets wrong on its own.
	    {
		mc_cell_t a2[2];
		int t;

		c = csp_leaf_buf(&state, (index_t)i);
		rc = run_bc(CSP_W_LEAF_BUF_ENTRY, (mc_cell_t)i, &b);
		if (rc != MC_OK || c != (int)b) {
		    printf("FAIL leaf_buf ix=%d: C says %d, bytecode %u (rc=%d)\n",
			   i, c, (unsigned)b, rc);
		    errors++;
		}

		c = (int)csp_array_len(&state, (index_t)i);
		rc = run_bc(CSP_W_ARRAY_LEN_ENTRY, (mc_cell_t)i, &b);
		if (rc != MC_OK || c != (int)b) {
		    printf("FAIL array_len nd=%d ix=%d: C says %d, bytecode %u"
			   " (rc=%d)\n", k, i, c, (unsigned)b, rc);
		    errors++;
		}
		for (t = 0; t <= NINSTR; t++) {
		    c = csp_gate_is_in(&state, (index_t)i, (index_t)t);
		    a2[0] = (mc_cell_t)i;
		    a2[1] = (mc_cell_t)t;
		    rc = run_bcn(CSP_W_GATE_IS_IN_ENTRY, a2, 2, &b);
		    if (rc != MC_OK || (c != 0) != (b != 0)) {
			printf("FAIL gate_is_in j=%d to=%d: C says %d,"
			       " bytecode %u (rc=%d)\n", i, t, c, (unsigned)b, rc);
			errors++;
		    }
		}
	    }

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

	    // A buffer field and a runtime count, neither of which a word could
	    // name before. i runs past NBUF on purpose: the word's own bounds
	    // test is the thing being compared.
	    c = csp_buf_owner_tag(&state, (index_t)i);
	    rc = run_bc(CSP_W_BUF_OWNER_TAG_ENTRY, (mc_cell_t)i, &b);
	    if (rc != MC_OK) {
		printf("FAIL buf_owner_tag ix=%d: bytecode stopped, rc=%d\n", i, rc);
		errors++;
	    } else if (c != (int)b) {
		printf("FAIL buf_owner_tag ix=%d: C says %d, bytecode says %u\n",
		       i, c, (unsigned)b);
		errors++;
	    }

	    // A THIRTY-TWO BIT field: the local that holds it, the constant it
	    // is compared against and the compare itself are all doubles, and
	    // nothing in the word says so.
	    if (i < NBUF) {
		c = csp_buf_is_xref(&state, (index_t)i);
		rc = run_bc(CSP_W_BUF_IS_XREF_ENTRY, (mc_cell_t)i, &b);
		if (rc != MC_OK) {
		    printf("FAIL buf_is_xref ix=%d: bytecode stopped, rc=%d\n", i, rc);
		    errors++;
		} else if (c != (int)b) {
		    printf("FAIL buf_is_xref ix=%d: C says %d, bytecode says %u\n",
			   i, c, (unsigned)b);
		    errors++;
		}
	    }

	    c = csp_local_number(&state, (index_t)i);
	    if (c != ref_local_number(&state, (index_t)i)) {
		printf("FAIL local_number nd=%d ix=%d: word says %d, the original %d\n",
		       k, i, c, ref_local_number(&state, (index_t)i));
		errors++;
	    }
	}
    }

    // ---------------------------------------------------------- WHAT IT PRINTS
    //
    // A word whose result is TEXT. Comparing the return value would prove
    // nothing -- both back ends return 0 -- so the console is recorded and the
    // two transcripts are compared. Pins are laid out so the word has both
    // cases to render: a consecutive run that collapses to `a..b`, and a break
    // that starts a new group.
    {
	char c_txt[sizeof(pr_buf)];
	mc_cell_t a2[2], b;
	int d, rc;
	static const char* want_pin[2] = { "0:0..3,1:4..7", "0:0..3,1:4..7" };

	for (i = 0; i < NDECL; i++) {
	    // Two runs on two ports: 0:0..3 then 1:4..7. That exercises BOTH
	    // paths -- the consecutive run that collapses to `a..b`, and the
	    // port break that starts a new group.
	    csp_decl_set_di_port(&table[i], (unsigned)(i / 4));
	    csp_decl_set_di_pin(&table[i], (unsigned)i);
	    csp_decl_set_cont(&table[i], (uint8_t)(i > 0));
	}
	for (d = 0; d < 2; d++) {
	    pr_n = 0; pr_buf[0] = 0;
	    csp_list_pin_spec(&state, 0, (index_t)d);
	    memcpy(c_txt, pr_buf, sizeof(pr_buf));

	    pr_n = 0; pr_buf[0] = 0;
	    a2[0] = 0; a2[1] = (mc_cell_t)d;
	    rc = run_bcn(CSP_W_LIST_PIN_SPEC_ENTRY, a2, 2, &b);
	    if (rc != MC_OK) {
		printf("FAIL list_pin_spec d=%d: bytecode stopped, rc=%d\n", d, rc);
		errors++;
	    } else if (strcmp(c_txt, pr_buf) != 0) {
		printf("FAIL list_pin_spec d=%d: C printed \"%s\", bytecode \"%s\"\n",
		       d, c_txt, pr_buf);
		errors++;
	    } else if (strcmp(c_txt, want_pin[d]) != 0) {
		// Agreement is not enough: both back ends come from one
		// description, so a wrong word is wrong in both. The TEXT is
		// what this word produces, so the text is what is pinned.
		printf("FAIL list_pin_spec d=%d: printed \"%s\", want \"%s\"\n",
		       d, c_txt, want_pin[d]);
		errors++;
	    }
	}
	// The table is left as the write checks below expect it.
	for (i = 0; i < NDECL; i++)
	    csp_decl_set_cont(&table[i], conts[i]);
    }

    // ------------------------------------------------------------- WRITES
    //
    // Agreement is not enough here: two back ends can be wrong the same way.
    // So the record is checked against what the word SAYS it does -- dir 3,
    // xref 0xDEADBEEF, owner the index -- and transport, which shares a byte
    // with dir and is never written, has to come back untouched.
    for (i = 0; i <= NBUF; i++) {
	static csp_buf_t after_c[NBUF];
	int cr, br, rc2;
	mc_cell_t bv;

	setup_bufs();
	cr = csp_buf_stamp(&state, (index_t)i);
	memcpy(after_c, bufs, sizeof(bufs));

	if (i < NBUF) {
	    if (csp_buf_get_dir(&after_c[i]) != 3) {
		printf("FAIL buf_stamp ix=%d: dir is %u, want 3\n",
		       i, (unsigned)csp_buf_get_dir(&after_c[i]));
		errors++;
	    }
	    if (csp_buf_get_xref(&after_c[i]) != 0xDEADBEEFUL) {
		printf("FAIL buf_stamp ix=%d: xref is %08lX, want DEADBEEF\n",
		       i, (unsigned long)csp_buf_get_xref(&after_c[i]));
		errors++;
	    }
	    if (csp_buf_get_owner(&after_c[i]) != (index_t)i) {
		printf("FAIL buf_stamp ix=%d: owner is %u\n",
		       i, (unsigned)csp_buf_get_owner(&after_c[i]));
		errors++;
	    }
	    // The NEIGHBOUR, in dir's own byte. A setter that stored a byte
	    // the field does not reach takes this with it.
	    if (cr != (int)(i + 1)) {
		printf("FAIL buf_stamp ix=%d: transport came back %d, want %d\n",
		       i, cr, i + 1);
		errors++;
	    }
	}

	setup_bufs();
	rc2 = run_bc(CSP_W_BUF_STAMP_ENTRY, (mc_cell_t)i, &bv);
	br = (int)bv;
	if (rc2 != MC_OK) {
	    printf("FAIL buf_stamp ix=%d: bytecode stopped, rc=%d\n", i, rc2);
	    errors++;
	} else if (cr != br) {
	    printf("FAIL buf_stamp ix=%d: C returns %d, bytecode %d\n", i, cr, br);
	    errors++;
	} else if (memcmp(after_c, bufs, sizeof(bufs)) != 0) {
	    unsigned k2;
	    const uint8_t* a = (const uint8_t*)after_c;
	    const uint8_t* b2 = (const uint8_t*)bufs;
	    for (k2 = 0; k2 < sizeof(bufs); k2++)
		if (a[k2] != b2[k2]) {
		    printf("FAIL buf_stamp ix=%d: byte %u is %02X in C, "
			   "%02X in bytecode\n", i, k2, a[k2], b2[k2]);
		    break;
		}
	    errors++;
	}
    }

    if (errors == 0)
	printf("ok\n");
    return (errors == 0) ? 0 : 1;
}
