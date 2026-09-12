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

// The DESCRIPTORS are checked by tests/layout.c, which compares every
// generated accessor against the C bit-field it replaces. What is left to
// check here is the other reader: mc_field_get walks (word, shift, bits) while
// the accessor walks byte offsets, and both are derived from the same row.
// They must not disagree.
static void test_fields(void)
{
    csp_decl_t d;

    memset(&d, 0, sizeof(d));
    csp_decl_set_type(&d, 0x0F);
    csp_decl_set_name(&d, 0x1FF);
    csp_decl_set_vt(&d, 0x0A);

    if (mc_field_get(&d, &mc_decl_fields[MFA_TYPE]) != csp_decl_get_type(&d))
	fail("mc_field_get type", mc_field_get(&d, &mc_decl_fields[MFA_TYPE]),
	     csp_decl_get_type(&d));
    if (mc_field_get(&d, &mc_decl_fields[MFA_NAME]) != csp_decl_get_name(&d))
	fail("mc_field_get name", mc_field_get(&d, &mc_decl_fields[MFA_NAME]),
	     csp_decl_get_name(&d));
    if (mc_field_get(&d, &mc_decl_fields[MFA_VT]) != csp_decl_get_vt(&d))
	fail("mc_field_get vt", mc_field_get(&d, &mc_decl_fields[MFA_VT]),
	     csp_decl_get_vt(&d));
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

static const void* my_decl(void* ctx, mc_cell_t i)
{
    (void)ctx;
    return (i == 0) ? &hook_decl : NULL;
}

static mc_cell_t my_nd(void* ctx)
{
    (void)ctx;
    return 7;
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

PROG(p_add,  MC_LIT8, 5, MC_LIT8, 3, MC_ADD, MC_BYE);
PROG(p_sub,  MC_LIT8, 5, MC_LIT8, 3, MC_SUB, MC_BYE);
PROG(p_lit16,MC_LIT16, 0x34, 0x12, MC_BYE);
PROG(p_swap, MC_LIT8, 5, MC_LIT8, 3, MC_SWAP, MC_SUB, MC_BYE);
PROG(p_over, MC_LIT8, 9, MC_LIT8, 4, MC_OVER, MC_ADD, MC_BYE);
PROG(p_wrap, MC_LIT16, 0xFF, 0xFF, MC_INC, MC_BYE);
PROG(p_lt,   MC_LIT8, 3, MC_LIT8, 5, MC_LT, MC_BYE);
PROG(p_ltu,  MC_LIT16, 0x00, 0x80, MC_LIT8, 1, MC_LT, MC_BYE);
PROG(p_nat,  MC_LIT8, 21, MC_NATIVE, 0, MC_BYE);
PROG(p_nd,   MC_ND, MC_BYE);

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

PROG(p_decl, MC_LIT8, 0, MC_DECL, MFA_TYPE, MC_BYE);
PROG(p_declbad, MC_LIT8, 1, MC_DECL, MFA_TYPE, MC_BYE);

PROG(p_sum3, MC_LIT8, 1, MC_LIT8, 2, MC_LIT8, 4, MC_NATIVEN, 0, MC_BYE);
// split leaves ( lo hi ) with hi on top; ADD folds them back to 0x12+0x34.
PROG(p_split, MC_LIT16, 0x34, 0x12, MC_NATIVEN, 1, MC_ADD, MC_BYE);
PROG(p_badleafn, MC_LIT8, 0, MC_NATIVEN, 9, MC_BYE);
PROG(p_runaway, MC_LIT8, 0, MC_NATIVEN, 2, MC_BYE);

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
    expect("stack leaf takes three", p_sum3, sizeof(p_sum3), MC_OK, 7);
    expect("stack leaf may push two", p_split, sizeof(p_split), MC_OK, 0x12 + 0x34);
    expect("unknown stack leaf stops", p_badleafn, sizeof(p_badleafn), MC_E_LEAF, 0);
    expect("a leaf that runs the stack off stops", p_runaway,
	   sizeof(p_runaway), MC_E_STACK, 0);
    expect("loop sums 1..5", p_loop, sizeof(p_loop), MC_OK, 15);
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
    mc_nd_hook = my_nd;
    test_fields();
    test_machine();
    if (errors == 0)
	printf("ok\n");
    return (errors == 0) ? 0 : 1;
}
