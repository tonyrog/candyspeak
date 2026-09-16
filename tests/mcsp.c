// micro-csp: the machine, and the field table it cannot derive.
//
// The field table is the reason this test exists. mc_decl_fields[] restates the
// bit layout of DECL_TYPE_HEADER by hand -- offsetof does not reach a bit-field,
// so there is no way to compute it -- and a wrong shift or width there is a
// silently wrong value at every site that reads that field. So the sweep below
// sets ONE field through the real C struct, then reads EVERY field back through
// its descriptor: the one must come out right and the others must come out
// zero. That catches a bad shift and a bad width in the same pass.
//
// Built by `make mcsp_check`. Prints "ok" and nothing else on success.

#include <stdio.h>
#include <string.h>
#include "csp.h"
#include "csp_layout.h"
#include "csp_mcsp.h"

static int errors = 0;

static void fail(const char* what, long got, long want)
{
    printf("FAIL %s: got %ld, want %ld\n", what, got, want);
    errors++;
}

// ---------------------------------------------------------------- field table

// The DESCRIPTORS are checked by tests/layout.c, which compares every generated
// accessor against the C bit-field it replaces. What is left to check here is
// the OTHER reader: mc_field_get walks {byte, bit, bits} while the accessor is
// a generated shift and mask, and both come from the same row in layout.terms.
//
// Every field, not a chosen few. This was three fields of thirty-six by hand,
// and a reader that gathered one byte where it needed three passed all three of
// them -- they were the byte-aligned ones somebody thought to write down. The
// macro is generated from the same terms file as the readers, so a field cannot
// be added without arriving here too.
//
// A PATTERN rather than one field at a time: with every byte distinct, a wrong
// byte offset or a wrong bit lands on a different value wherever it is.
#define PATTERN(REC) do { \
	unsigned k_; \
	for (k_ = 0; k_ < sizeof(REC); k_++) \
	    ((uint8_t*)&(REC))[k_] = (uint8_t)(0x5A + k_ * 0x27); \
    } while (0)

// value_t's rows. NOT in src/csp_mcsp.c: no opcode reaches a value yet, and a
// table the machine never reads has no business in every port's flash. When
// MC_VALUE arrives this moves there with the other four.
static const mc_field_t mc_value_fields[] = CSP_VALUE_ALL_FIELDS;

static void test_fields(void)
{
    csp_decl_t  d;
    csp_instr_t i;
    csp_buf_t   b;
    csp_view_t  v;

    PATTERN(d); CSP_MCFIELD_DECL(d, fail);
    PATTERN(i); CSP_MCFIELD_INSTR(i, fail);
    PATTERN(b); CSP_MCFIELD_BUF(b, fail);
    PATTERN(v); CSP_MCFIELD_VIEW(v, fail);
    { value_t w; PATTERN(w); CSP_MCFIELD_VALUE(w, fail); }
}

// ---------------------------------------------------------------- the writes

// Two fields OVERLAP when their bit ranges do. Declaration arms are unions, so
// writing tm.period is supposed to change va.init -- those two share the bytes
// on purpose. What must not change is a field that shares no bit with the one
// written, and the interesting ones are exactly those that share a BYTE with
// it: a setter that stores a whole byte takes its neighbour with it, and
// nothing else in the suite would notice.
static int overlaps(const mc_field_t* a, const mc_field_t* b)
{
    unsigned alo = (unsigned)a->byte * 8 + a->bit;
    unsigned blo = (unsigned)b->byte * 8 + b->bit;

    return !((alo + a->bits <= blo) || (blo + b->bits <= alo));
}

static void set_family(const char* fam, const mc_field_t* tab,
		       uint8_t n, uint8_t nd, void* rec, unsigned size)
{
    static mc_cell_t before[64];
    // Three values, because one is not enough: all-ones would pass a mask that
    // is too wide, and a single pattern would pass a shift that is off by a
    // whole field width in a field whose bits happen to repeat.
    static const mc_cell_t vals[3] = { 0xFFFF, 0xA5A5, 0x0001 };
    unsigned f, g, k, q;
    char what[64];

    for (k = 0; k < 3; k++)
	for (f = nd; f < n; f++) {
	    mc_cell_t m = (tab[f].bits >= 16)
		? 0xFFFFu : (mc_cell_t)((1u << tab[f].bits) - 1u);
	    mc_cell_t want = (mc_cell_t)(vals[k] & m);

	    for (q = 0; q < size; q++)
		((uint8_t*)rec)[q] = (uint8_t)(0x5A + q * 0x27);
	    for (g = 0; g < n; g++)
		before[g] = mc_field_get(rec, &tab[g]);

	    mc_field_set(rec, &tab[f], vals[k]);

	    if (mc_field_get(rec, &tab[f]) != want) {
		sprintf(what, "%s set[%u] v=%04X", fam, f, (unsigned)vals[k]);
		fail(what, (long)mc_field_get(rec, &tab[f]), (long)want);
	    }
	    for (g = 0; g < n; g++)
		if ((g != f) && !overlaps(&tab[f], &tab[g]) &&
		    (mc_field_get(rec, &tab[g]) != before[g])) {
		    sprintf(what, "%s set[%u] spilled into [%u]", fam, f, g);
		    fail(what, (long)mc_field_get(rec, &tab[g]), (long)before[g]);
		}
	}
}

static void test_field_set(void)
{
    csp_decl_t  d;
    csp_instr_t i;
    csp_buf_t   b;
    csp_view_t  v;
    value_t     w;

    set_family("decl",  mc_decl_fields,  mc_decl_nfield,  mc_decl_ndouble,
	       &d, sizeof(d));
    set_family("instr", mc_instr_fields, mc_instr_nfield, mc_instr_ndouble,
	       &i, sizeof(i));
    set_family("buf",   mc_buf_fields,   mc_buf_nfield,   mc_buf_ndouble,
	       &b, sizeof(b));
    set_family("view",  mc_view_fields,  mc_view_nfield,  mc_view_ndouble,
	       &v, sizeof(v));
    set_family("value", mc_value_fields, MFV_NFIELD, MFV_NDOUBLE,
	       &w, sizeof(w));
}

// ------------------------------------------------------------------- machine

static mc_cell_t ds[16];
static uint16_t  rs[8];

static mc_cell_t leaf_double(void* ctx, mc_cell_t tos)
{
    (void)ctx;
    return (mc_cell_t)(tos * 2);
}

static const mc_leaf_t leaves[] = { leaf_double };

// A stack-form leaf: three in, one out. It decides its own arity, and the new
// stack pointer is how it says so -- one register carrying both what it ate
// and what it left. sp[0] is the top, like on AVR.
static mc_cell_t* leafn_sum3(void* ctx, mc_cell_t* sp)
{
    mc_cell_t v = (mc_cell_t)(sp[0] + sp[1] + sp[2]);

    (void)ctx;
    sp += 2;            // three consumed, one left
    sp[0] = v;
    return sp;
}

// And one that pushes MORE than it pops, which an (npop, value) convention
// cannot express at all.
static mc_cell_t* leafn_split(void* ctx, mc_cell_t* sp)
{
    mc_cell_t v = sp[0];

    (void)ctx;
    sp--;
    sp[0] = (mc_cell_t)(v & 0xFF);
    sp[1] = (mc_cell_t)(v >> 8);
    return sp;
}

// A BROKEN leaf, on purpose. A stack-form leaf decides its own arity, so
// nothing outside it knows what it should have left -- the only thing the
// machine can check is that the pointer is still inside the array. This is
// what proves that check runs.
static mc_cell_t* leafn_runaway(void* ctx, mc_cell_t* sp)
{
    (void)ctx;
    return sp - 100;
}

static const mc_leafn_t leavesn[] = { leafn_sum3, leafn_split, leafn_runaway };

static csp_decl_t hook_decl;

// This file tests the MACHINE, not the words, so the state hook is local: what
// a real runtime answers is utils/words.terms' business and tests/words.c
// checks that. Here the numbers only have to be distinguishable.
static mc_cell_t my_state(void* ctx, mc_cell_t i)
{
    (void)ctx;
    switch (i) {
    case 0:  return 7;
    case 1:  return 2;
    default: return 3;
    }
}

static const void* my_decl(void* ctx, mc_cell_t i)
{
    (void)ctx;
    return (i == 0) ? &hook_decl : NULL;
}

static int run(const uint8_t* code, uint16_t len, mc_cell_t* out)
{
    mc_vm_t vm;

    memset(&vm, 0, sizeof(vm));
    vm.code = code;
    vm.code_len = len;
    vm.ds = ds;
    vm.ds_size = (uint8_t)(sizeof(ds) / sizeof(ds[0]));
    vm.rs = rs;
    vm.rs_size = (uint8_t)(sizeof(rs) / sizeof(rs[0]));
    vm.leaf = leaves;
    vm.nleaf = 1;
    vm.leafn = leavesn;
    vm.nleafn = (uint8_t)(sizeof(leavesn) / sizeof(leavesn[0]));
    return csp_mcsp_run(&vm, 0, out);
}

static void expect(const char* what, const uint8_t* code, uint16_t len,
		   int rc_want, long want)
{
    mc_cell_t got = 0;
    int rc = run(code, len, &got);

    if (rc != rc_want) {
	fail(what, rc, rc_want);
	return;
    }
    if ((rc == MC_OK) && ((long)got != want))
	fail(what, (long)got, want);
}

#define PROG(name, ...) \
    static const uint8_t name[] = { __VA_ARGS__ }

// THE LONG BRANCHES. Generated code uses them only where the one-byte
// displacement does not reach, so a hand-written program is the only place
// their semantics get stated: the displacement is added to ip AFTER both
// operand bytes, exactly like the short form.
//
//  0 LIT8 1 | 2 JMP16 +4 | 5 DROP | 6 LIT8 9 | 8 BYE | 9 LIT8 7 | 11 BYE
PROG(p_jmp16, MC_LIT8, 1, MC_JMP16, 4, 0,
     MC_DROP, MC_LIT8, 9, MC_BYE, MC_LIT8, 7, MC_BYE);
//  0 LIT8 0 | 2 JZ16 +3 | 5 LIT8 9 | 7 BYE | 8 LIT8 7 | 10 BYE
PROG(p_jz16, MC_LIT8, 0, MC_JZ16, 3, 0,
     MC_LIT8, 9, MC_BYE, MC_LIT8, 7, MC_BYE);
// The same, NOT taken: a non-zero top falls through to the 9.
PROG(p_jz16n, MC_LIT8, 1, MC_JZ16, 3, 0,
     MC_LIT8, 9, MC_BYE, MC_LIT8, 7, MC_BYE);
// BACKWARDS, which is what a relaxed loop edge becomes. Counts to 3.
//
//  0 LIT8 0 | 2 LIT8 1 | 4 ADD | 5 DUP | 6 LIT8 3 | 8 LT | 9 JZ +3 |
// 11 JMP16 -12 | 14 BYE
//
// Both displacements are measured from ip AFTER the operands: 11 + 3 = 14,
// and 14 - 12 = 2. Getting that wrong does not fail, it HANGS -- there is no
// step budget in the machine -- which is its own reason to state the arithmetic
// here rather than leave it to be re-derived.
PROG(p_jmp16b, MC_LIT8, 0,
     MC_LIT8, 1, MC_ADD, MC_DUP, MC_LIT8, 3, MC_LT, MC_JZ, 3,
     MC_JMP16, (uint8_t)-12, (uint8_t)-1, MC_BYE);
PROG(p_add,  MC_LIT8, 5, MC_LIT8, 3, MC_ADD, MC_BYE);
PROG(p_sub,  MC_LIT8, 5, MC_LIT8, 3, MC_SUB, MC_BYE);
PROG(p_lit16,MC_LIT16, 0x34, 0x12, MC_BYE);
PROG(p_swap, MC_LIT8, 5, MC_LIT8, 3, MC_SWAP, MC_SUB, MC_BYE);
PROG(p_over, MC_LIT8, 9, MC_LIT8, 4, MC_OVER, MC_ADD, MC_BYE);
PROG(p_wrap, MC_LIT16, 0xFF, 0xFF, MC_INC, MC_BYE);
PROG(p_lt,   MC_LIT8, 3, MC_LIT8, 5, MC_LT, MC_BYE);
PROG(p_ltu,  MC_LIT16, 0x00, 0x80, MC_LIT8, 1, MC_LT, MC_BYE);
PROG(p_nat,  MC_LIT8, 21, MC_NATIVE, 0, MC_BYE);
// MC_ST with the state id the local hook answers. MC_ND was a separate
// opcode that meant `state 0`, i.e. a hardcoded index into a table the
// generator builds -- it stopped meaning nd the moment that table stopped
// listing every field.
PROG(p_nd,   MC_ST, 0, MC_BYE);

// sum 1..5 through the return stack.  offsets:
//  0 LIT8 0 | 2 LIT8 5 | 4 TOR | 5 RAT | 6 JZ +7 | 8 RAT | 9 ADD | 10 RFROM
// 11 DEC | 12 TOR | 13 JMP -10 | 15 RFROM | 16 DROP | 17 BYE
PROG(p_loop,
     MC_LIT8, 0, MC_LIT8, 5, MC_TOR,
     MC_RAT, MC_JZ, 7,
     MC_RAT, MC_ADD, MC_RFROM, MC_DEC, MC_TOR, MC_JMP, (uint8_t)-10,
     MC_RFROM, MC_DROP, MC_BYE);

// MC_CALL carries the CALLER's frame size as a third operand, so the word sits
// one byte further along than it used to.
//  0 LIT8 10 | 2 CALL 7,0 frame 0 | 6 BYE | 7 INC | 8 INC | 9 EXIT
PROG(p_call, MC_LIT8, 10, MC_CALL, 7, 0, 0, MC_BYE, MC_INC, MC_INC, MC_EXIT);

PROG(p_decl, MC_LIT8, 0, MC_DECL, MFD_TYPE, MC_BYE);
PROG(p_declbad, MC_LIT8, 1, MC_DECL, MFD_TYPE, MC_BYE);

PROG(p_sum3, MC_LIT8, 1, MC_LIT8, 2, MC_LIT8, 4, MC_NATIVEN, 0, MC_BYE);
// split leaves ( lo hi ) with hi on top; ADD folds them back to 0x12+0x34.
PROG(p_split, MC_LIT16, 0x34, 0x12, MC_NATIVEN, 1, MC_ADD, MC_BYE);
PROG(p_badleafn, MC_LIT8, 0, MC_NATIVEN, 9, MC_BYE);
PROG(p_runaway, MC_LIT8, 0, MC_NATIVEN, 2, MC_BYE);

// DOUBLES. A double is two cells with the high half on top, so a program that
// leaves one and stops reports only the HIGH cell through MC_BYE -- these
// therefore reduce to a single cell before finishing.
PROG(p_dlit,  MC_DLIT, 0x78,0x56,0x34,0x12, MC_DROP, MC_BYE);        // low half
PROG(p_dlith, MC_DLIT, 0x78,0x56,0x34,0x12, MC_BYE);                 // high half
PROG(p_s2d,   MC_LIT16, 0x34,0x12, MC_S2D, MC_BYE);                  // high = 0
PROG(p_dadd,  MC_DLIT, 0xFF,0xFF,0x00,0x00, MC_DLIT, 0x01,0x00,0x00,0x00,
              MC_DADD, MC_BYE);                                      // carry out
PROG(p_dsub,  MC_DLIT, 0x00,0x00,0x01,0x00, MC_DLIT, 0x01,0x00,0x00,0x00,
              MC_DSUB, MC_DROP, MC_BYE);                             // borrow in
PROG(p_deq,   MC_DLIT, 0x78,0x56,0x34,0x12, MC_DLIT, 0x78,0x56,0x34,0x12,
              MC_DEQ, MC_BYE);
PROG(p_dne,   MC_DLIT, 0x78,0x56,0x34,0x12, MC_DLIT, 0x78,0x56,0x34,0x13,
              MC_DEQ, MC_BYE);
PROG(p_dlt,   MC_DLIT, 0xFF,0xFF,0x00,0x00, MC_DLIT, 0x00,0x00,0x01,0x00,
              MC_DLT, MC_BYE);                                       // differ in HIGH
PROG(p_dge,   MC_DLIT, 0x00,0x00,0x01,0x00, MC_DLIT, 0xFF,0xFF,0x00,0x00,
              MC_DLT, MC_BYE);
PROG(p_ddrop, MC_LIT8, 9, MC_DLIT, 0x78,0x56,0x34,0x12, MC_DDROP, MC_BYE);
PROG(p_dunder, MC_DLIT, 0x01,0x00,0x00,0x00, MC_DADD, MC_BYE);

PROG(p_badop, 200, MC_BYE);
PROG(p_under, MC_ADD, MC_BYE);
PROG(p_runoff, MC_LIT8, 1, MC_INC);      // no BYE: runs off the end

static void test_machine(void)
{
    expect("add",        p_add,   sizeof(p_add),   MC_OK, 8);
    expect("sub",        p_sub,   sizeof(p_sub),   MC_OK, 2);
    expect("lit16",      p_lit16, sizeof(p_lit16), MC_OK, 0x1234);
    expect("swap",       p_swap,  sizeof(p_swap),  MC_OK, (mc_cell_t)(3 - 5));
    expect("over",       p_over,  sizeof(p_over),  MC_OK, 13);
    expect("cells wrap at 16 bits", p_wrap, sizeof(p_wrap), MC_OK, 0);
    expect("lt",         p_lt,    sizeof(p_lt),    MC_OK, 1);
    expect("lt is unsigned", p_ltu, sizeof(p_ltu), MC_OK, 0);
    expect("native",     p_nat,   sizeof(p_nat),   MC_OK, 42);
    expect("nd hook",    p_nd,    sizeof(p_nd),    MC_OK, 7);

    expect("dlit low half",   p_dlit,  sizeof(p_dlit),  MC_OK, 0x5678);
    expect("dlit high half",  p_dlith, sizeof(p_dlith), MC_OK, 0x1234);
    expect("s2d zero-extends", p_s2d,  sizeof(p_s2d),   MC_OK, 0);
    expect("dadd carries into the high cell", p_dadd, sizeof(p_dadd), MC_OK, 1);
    expect("dsub borrows from it",  p_dsub,  sizeof(p_dsub),  MC_OK, 0xFFFF);
    expect("deq",             p_deq,   sizeof(p_deq),   MC_OK, 1);
    expect("deq sees the high cell", p_dne, sizeof(p_dne), MC_OK, 0);
    expect("dlt compares both cells", p_dlt, sizeof(p_dlt), MC_OK, 1);
    expect("and the other way",  p_dge, sizeof(p_dge), MC_OK, 0);
    expect("ddrop takes two",  p_ddrop, sizeof(p_ddrop), MC_OK, 9);
    expect("dadd on one double stops", p_dunder, sizeof(p_dunder), MC_E_STACK, 0);
    expect("stack leaf takes three", p_sum3, sizeof(p_sum3), MC_OK, 7);
    expect("stack leaf may push two", p_split, sizeof(p_split), MC_OK, 0x12 + 0x34);
    expect("unknown stack leaf stops", p_badleafn, sizeof(p_badleafn), MC_E_LEAF, 0);
    expect("a leaf that runs the stack off stops", p_runaway,
	   sizeof(p_runaway), MC_E_STACK, 0);
    expect("loop sums 1..5", p_loop, sizeof(p_loop), MC_OK, 15);
    expect("jmp16 jumps forward",  p_jmp16,  sizeof(p_jmp16),  MC_OK, 7);
    expect("jz16 taken",           p_jz16,   sizeof(p_jz16),   MC_OK, 7);
    expect("jz16 not taken",       p_jz16n,  sizeof(p_jz16n),  MC_OK, 9);
    expect("jmp16 jumps backward", p_jmp16b, sizeof(p_jmp16b), MC_OK, 3);
    expect("call/exit",  p_call,  sizeof(p_call),  MC_OK, 12);

    csp_decl_set_type(&hook_decl, DECL_BUFFER);
    expect("decl field through the hook", p_decl, sizeof(p_decl),
	   MC_OK, (long)DECL_BUFFER);

    // Each of these is a GENERATOR bug, not user input, so the machine stops
    // rather than limping on with a plausible number.
    expect("a decl index the hook rejects stops", p_declbad,
	   sizeof(p_declbad), MC_E_BOUNDS, 0);
    expect("bad token stops",       p_badop,   sizeof(p_badop),   MC_E_OPCODE, 0);
    expect("underflow stops",       p_under,   sizeof(p_under),   MC_E_STACK, 0);
    expect("running off the end stops", p_runoff, sizeof(p_runoff), MC_E_BOUNDS, 0);
}

int main(void)
{
    mc_decl_hook = my_decl;
    mc_state_hook = my_state;
    test_fields();
    test_field_set();
    test_machine();
    if (errors == 0)
	printf("ok\n");
    return (errors == 0) ? 0 : 1;
}
