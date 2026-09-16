// micro-csp: the portable C interpreter. See include/csp_mcsp.h for what the
// machine is for and why the cells are sixteen bits.
//
// This file is the REFERENCE. The AVR dispatch is six instructions and a
// 256-entry rjmp table; it is faster and smaller and it is not this. What keeps
// them the same machine is that both run the bytecode `make test` runs here.

#include <string.h>
#include "csp.h"
#include "csp_layout.h"
#include "csp_mcsp.h"

// The field table is GENERATED. It used to be written out here -- positions
// derived by hand from the widths in csp.h -- and a wrong shift returns the
// neighbouring field's bits with nothing to notice. utils/layout.terms states
// the layout once; gen/csp_layout.h computes the positions and the accessors
// from it, and tests/layout.c checks both against the struct they replace.
// The COUNTS are compile-time constants -- the machine compares against
// MF*_NFIELD directly, which is an immediate rather than a load, and these
// exist only so a test can walk a table without including the generated
// header. Nothing in a firmware references them, so --gc-sections takes
// them and the eight bytes of RAM they would otherwise hold.
const mc_field_t mc_decl_fields[] RODATA = CSP_DECL_ALL_FIELDS;
const uint8_t    mc_decl_nfield = (uint8_t)MFD_NFIELD;
const uint8_t    mc_decl_ndouble = (uint8_t)MFD_NDOUBLE;

const mc_field_t mc_instr_fields[] RODATA = CSP_INSTR_ALL_FIELDS;
const uint8_t    mc_instr_nfield = (uint8_t)MFI_NFIELD;
const uint8_t    mc_instr_ndouble = (uint8_t)MFI_NDOUBLE;

const mc_field_t mc_buf_fields[] RODATA = CSP_BUF_ALL_FIELDS;
const uint8_t    mc_buf_nfield = (uint8_t)MFB_NFIELD;
const uint8_t    mc_buf_ndouble = (uint8_t)MFB_NDOUBLE;

// MFW, not MFV: the view family's tag is w. MFV is value_t, which has sixteen
// rows where a view has seven -- a walk of this table with that count reads
// nine rows past its end.
const mc_field_t mc_view_fields[] RODATA = CSP_VIEW_ALL_FIELDS;
const uint8_t    mc_view_nfield = (uint8_t)MFW_NFIELD;
const uint8_t    mc_view_ndouble = (uint8_t)MFW_NDOUBLE;


// The record is read a BYTE AT A TIME into a uint32_t rather than cast to one.
// A csp_decl_t is PACKED and may sit at any address in the pool, and a 32-bit
// load from an odd address is a fault on the ARM ports -- which is how a packed
// function-pointer table once turned into a HardFault. memcpy says the same
// thing and compiles to the same code where the alignment is known.
// The mask a width needs. A table rather than (1 << bits) - 1, because a
// variable shift on AVR is a LOOP and this one ran on every access -- and once
// the gather below stopped branching three ways, the loop was most of what was
// left. Thirty-four bytes of flash against that.
static const uint16_t mc_mask[17] RODATA = {
    0x0000, 0x0001, 0x0003, 0x0007, 0x000F, 0x001F, 0x003F, 0x007F,
    0x00FF, 0x01FF, 0x03FF, 0x07FF, 0x0FFF, 0x1FFF, 0x3FFF, 0x7FFF,
    0xFFFF
};

// The DESCRIPTOR is in flash too, so its three bytes are read the same way.
// Into a local first: the walk below uses `bit` and `bits` several times each,
// and a flash read per use would be three lpm apiece.
static void mc_field_load_k(const mc_field_t* f, mc_field_t* g, uint8_t k)
{
    g->byte = ro_byte(&f->byte);
    g->bit  = ro_byte(&f->bit);
    g->bits = ro_byte(&f->bits);
    // An ARRAY field's row describes element zero; k walks it. Whole bytes by
    // construction (the layout generator refuses anything else), so this is an
    // add and not a shift.
    g->byte = (uint8_t)(g->byte + k * (uint8_t)(g->bits >> 3));
}

#define mc_field_load(f, g) mc_field_load_k((f), (g), 0)

// TWO BYTES, and never a third. utils/layout.terms is padded so no field needs
// one and gen_layout refuses to emit a descriptor that would -- which is what
// lets this be one compare instead of a byte count and two branches, and what
// keeps a field in a record's LAST byte from reading the record after it.
mc_cell_t mc_field_get_k(const void* rec, const mc_field_t* fp, uint8_t k)
{
    mc_field_t f;
    const uint8_t* p;
    uint16_t w;

    mc_field_load_k(fp, &f, k);
    p = (const uint8_t*)rec + f.byte;
    w = p[0];
    if ((uint8_t)(f.bit + f.bits) > 8)
	w |= (uint16_t)p[1] << 8;
    return (mc_cell_t)((w >> f.bit) & ro_word(&mc_mask[f.bits]));
}

// The mirror. It must not write a byte the field does not reach: the neighbour
// is usually another field of the same record, and for a field at the end of
// one, another record.
void mc_field_set_k(void* rec, const mc_field_t* fp, mc_cell_t v, uint8_t k)
{
    mc_field_t f;
    uint8_t* p;
    uint16_t m, w;
    uint8_t wide;

    mc_field_load_k(fp, &f, k);
    p = (uint8_t*)rec + f.byte;
    m = ro_word(&mc_mask[f.bits]);
    wide = (uint8_t)((uint8_t)(f.bit + f.bits) > 8);
    w = p[0];
    if (wide)
	w |= (uint16_t)p[1] << 8;
    w = (uint16_t)((w & (uint16_t)~(uint16_t)(m << f.bit))
		   | (uint16_t)((uint16_t)(v & m) << f.bit));
    p[0] = (uint8_t)w;
    if (wide)
	p[1] = (uint8_t)(w >> 8);
}



// THE FOUR RECORD FAMILIES, as data. What the opcodes differed in was which
// hook, which field table and how long it is -- three values, so they are a
// table of three values and the sixteen bodies become four.
//
// The hooks are POINTERS SET AT RUNTIME, so they are an array of their own in
// RAM; the descriptions are constant and live in flash beside the field tables
// they name. The old spellings (mc_decl_hook, mc_buf_wr, ...) are macros over
// the array elements, so every install site and every test reads as before.
typedef struct {
    const mc_field_t* fields;
    uint8_t nfield;
    uint8_t ndouble;
} mc_fam_t;

static const mc_fam_t mc_fam[4] RODATA = {
    { mc_decl_fields,  (uint8_t)MFD_NFIELD, (uint8_t)MFD_NDOUBLE },
    { mc_instr_fields, (uint8_t)MFI_NFIELD, (uint8_t)MFI_NDOUBLE },
    { mc_buf_fields,   (uint8_t)MFB_NFIELD, (uint8_t)MFB_NDOUBLE },
    { mc_view_fields,  (uint8_t)MFW_NFIELD, (uint8_t)MFW_NDOUBLE }
};

const void* (*mc_rd_hook[4])(void* ctx, mc_cell_t i);
void*       (*mc_wr_hook[4])(void* ctx, mc_cell_t i);

static uint8_t fam_n(uint8_t f)  { return ro_byte(&mc_fam[f].nfield); }
static uint8_t fam_nd(uint8_t f) { return ro_byte(&mc_fam[f].ndouble); }

static const mc_field_t* fam_row(uint8_t f, uint8_t i)
{
    return &((const mc_field_t*)ro_ptr(&mc_fam[f].fields))[i];
}

// Hooks the domain words reach the runtime through. Indirection rather than a
// direct call to csp_decl_ref so tests/mcsp.c can exercise the machine without
// linking the whole runtime behind it -- the field table is the part that can
// be quietly wrong, and it deserves a test with nothing else in the way.
mc_cell_t   (*mc_state_hook)(void* ctx, mc_cell_t i);

// The write side, deliberately separate: see the note on MC_DECLS.
void  (*mc_state_set)(void* ctx, mc_cell_t i, mc_cell_t v);
mc_cell_t (*mc_array_hook)(void* ctx, mc_cell_t id, mc_cell_t ix);
void      (*mc_array_set)(void* ctx, mc_cell_t id, mc_cell_t ix,
				 mc_cell_t v);

// The record, read or write, through the family's hook. NULL when there is no
// hook at all and NULL when the index names nothing -- the caller tells those
// apart by asking, which is what e_leaf_or_bounds is for.
static const void* fam_rd(mc_vm_t* vm, uint8_t f, mc_cell_t i)
{
    return (mc_rd_hook[f] == NULL) ? NULL : mc_rd_hook[f](vm->ctx, i);
}

static void* fam_wr(mc_vm_t* vm, uint8_t f, mc_cell_t i)
{
    return (mc_wr_hook[f] == NULL) ? NULL : mc_wr_hook[f](vm->ctx, i);
}

// THE REFERENCE DOES NOT CACHE TOS. The AVR dispatch holds the top cell in
// r24:r25 and that is worth real bytes there, but here it buys nothing and
// costs correctness: with a cached top, "empty" is a state the array cannot
// represent, and every push onto an empty stack writes a garbage cell. Caching
// is an implementation choice, not semantics.
//
// The DIRECTION is not an implementation choice. The stack grows down, sp
// points AT the top cell, and sp[1] is the one below -- the same view a
// stack-form leaf gets on AVR, where Y is the stack pointer. A leaf is
// ordinary C compiled for both machines and must not see two layouts.
#define DS_BASE  (vm->ds)
#define DS_TOP   (vm->ds + vm->ds_size)

#define PUSH(v)  do {					\
	mc_cell_t v_ = (v);				\
	if (sp <= DS_BASE) goto e_stack;		\
	*--sp = v_;					\
    } while (0)

#define POP(lv)  do {					\
	if (sp >= DS_TOP) goto e_stack;			\
	(lv) = *sp++;					\
    } while (0)

#define NEED(n)  do {					\
	if ((DS_TOP - sp) < (n)) goto e_stack;		\
    } while (0)

#define GOTO_PUSH_TMP(x) do {			\
	tmp = (x);				\
	goto lbl_push_tmp;			\
    } while(0)

// THE TOKEN STREAM IS IN FLASH. On AVR a plain `const uint8_t[]` is .rodata,
// which the linker script puts inside .data -- copied into RAM at startup and
// held there for the life of the program. Three hundred bytes of a two-kilobyte
// part, for a table that is never written. ro_byte is the project's accessor for
// exactly this and costs nothing on a host, where it is a dereference.
#define FETCH8(lv) do {					\
	if (ip >= vm->code_len) goto e_bounds;		\
	(lv) = ro_byte(&vm->code[ip]); ip++;		\
    } while (0)

// WHAT A NESTED RUN NEEDS. A native can call a word, and that word runs on
// these same three arrays -- so before handing control to one, say how much of
// each is spoken for. Only here: everywhere else the machine's own locals are
// the truth and these fields are stale.
#define MARK() do { vm->sp = sp; vm->rp = rp; vm->lvtop = lvt; } while (0)

int csp_mcsp_run(mc_vm_t* vm, uint16_t entry, mc_cell_t* result)
{
    uint16_t ip = entry;
    mc_cell_t* sp = vm->ds + vm->ds_size;   // empty
    uint8_t  rp = 0;
    uint8_t  lvb = 0;                       // base of the current local frame
    uint8_t  lvt = vm->frame;               // and the first slot above it
    uint8_t  op;
#ifdef CSP_MCSP_STEPS
    uint32_t steps_ = 0;
#endif
    uint8_t  b;
    uint8_t  f_ = 0;
    mc_cell_t a;
    mc_cell_t tmp;

    for (b = 0; b < vm->nargs; b++)
	PUSH(vm->arg[b]);
next:
#ifdef CSP_MCSP_STEPS
    if (++steps_ > (uint32_t)(CSP_MCSP_STEPS))
	return MC_E_STEPS;
#endif
    FETCH8(op);
    switch (op) {
    case MC_BYE:
	NEED(1);
	goto lbl_result;
    // INDEX(): the object bits masked off a packed declaration index. In the
    // stream it was LIT16 lo hi AND -- four bytes, and the commonest phrase
    // there by a distance. The mask is a constant of the runtime, so it does
    // not belong in the token stream at all.
    case MC_INDEX:
	NEED(1);
	sp[0] = (mc_cell_t)INDEX(sp[0]);
	break;
    case MC_S2D: // push a zero
    case MC_ZERO: GOTO_PUSH_TMP(0);
    case MC_ONE:  GOTO_PUSH_TMP(1);
    case MC_LIT8:
	FETCH8(b);
	GOTO_PUSH_TMP(b);
    case MC_LIT16:
	FETCH8(b);
	a = (mc_cell_t)b;
	FETCH8(b);
	GOTO_PUSH_TMP((mc_cell_t)(a | ((mc_cell_t)b << 8)));
    case MC_DROP:
	POP(a);
	break;
    case MC_DUP:
	NEED(1);
	GOTO_PUSH_TMP(sp[0]);
    case MC_SWAP:
	NEED(2);
	a = sp[0]; sp[0] = sp[1]; sp[1] = a;
	break;
    case MC_OVER:
	NEED(2);
	GOTO_PUSH_TMP(sp[1]);
    case MC_ADD:
	NEED(2);
	sp[1] = (mc_cell_t)(sp[1] + sp[0]); sp++;
	break;
    case MC_SUB:                       // ( a b -- a-b )
	NEED(2);
	sp[1] = (mc_cell_t)(sp[1] - sp[0]); sp++;
	break;
    case MC_AND:
	NEED(2);
	sp[1] = (mc_cell_t)(sp[1] & sp[0]); sp++;
	break;
    case MC_OR:
	NEED(2);
	sp[1] = (mc_cell_t)(sp[1] | sp[0]); sp++;
	break;
    case MC_INC:
	NEED(1); sp[0]++;
	break;
    case MC_DEC:
	NEED(1); sp[0]--;
	break;
    case MC_EQ:
	NEED(2);
	sp[1] = (mc_cell_t)(sp[1] == sp[0]); sp++;
	break;
    case MC_NE:
	NEED(2);
	sp[1] = (mc_cell_t)(sp[1] != sp[0]); sp++;
	break;
    case MC_LT:                        // UNSIGNED, like the cell
	NEED(2);
	sp[1] = (mc_cell_t)(sp[1] < sp[0]); sp++;
	break;
    case MC_ZEQ:
	NEED(1);
	sp[0] = (mc_cell_t)(sp[0] == 0);
	break;
    // ONE BODY for both: a jump is a conditional jump whose condition is
    // already false. Setting a to zero and falling into the test costs two
    // instructions and saves the whole displacement path.
    case MC_JMP:
	a = 0;
	goto jump8;
    case MC_JZ:
	POP(a);
    jump8:
	FETCH8(b);
	if (a == 0)
	    ip = (uint16_t)(ip + (int8_t)b);
	break;
    // The same two, with a sixteen-bit displacement. Emitted only where the
    // short form does not reach -- a word big enough to need one is rare, and
    // paying the extra byte everywhere would not be.
    // The same two, with a sixteen-bit displacement, and merged the same way.
    case MC_JMP16:
	a = 0;
	goto jump16;
    case MC_JZ16:
	POP(a);
    jump16: {
	uint16_t d_;
	FETCH8(b); d_ = b;
	FETCH8(b); d_ |= (uint16_t)b << 8;
	if (a == 0)
	    ip = (uint16_t)(ip + (int16_t)d_);
	break;
    }
    case MC_CALL: {
	uint8_t frame;
	FETCH8(b);
	a = (mc_cell_t)b;
	FETCH8(b);
	FETCH8(frame);        // the CALLEE's, so lvt says where its frame ends
	// Two entries per call: the return address and the frame base. A
	// return stack sized for N calls therefore holds N/2 of them, which
	// is worth knowing when a port picks the number. The old TOP is not
	// saved: it is the base being pushed, read the other way round.
	if ((uint8_t)(rp + 2) > vm->rs_size) goto e_stack;
	if ((uint16_t)lvt + frame > vm->lv_size) goto e_bounds;
	vm->rs[rp++] = ip;
	vm->rs[rp++] = lvb;
	lvb = lvt;
	lvt = (uint8_t)(lvb + frame);
	ip = (uint16_t)(a | ((uint16_t)b << 8));
	break;
    }
    case MC_EXIT:
	// An empty return stack means this is the word the run started in,
	// so its return IS the run's result. A word that ended with BYE
	// instead could never be called from another word.
	if (rp == 0) {
	    NEED(1);
	    goto lbl_result;	    
	}
	if (rp < 2) goto e_stack;
	lvt = lvb;                          // this frame's base is the caller's top
	lvb = (uint8_t)vm->rs[--rp];
	ip = vm->rs[--rp];
	break;
    case MC_TOR:
	POP(a);
	if (rp >= vm->rs_size) goto e_stack;
	vm->rs[rp++] = a;
	break;
    case MC_RFROM:
	if (rp == 0) goto e_stack;
	GOTO_PUSH_TMP((mc_cell_t)vm->rs[--rp]);
    case MC_RAT:
	if (rp == 0) goto e_stack;
	GOTO_PUSH_TMP((mc_cell_t)vm->rs[rp - 1]);

    // ONE BODY PER SHAPE, not per record type. Reading a field out of a
    // declaration and out of a buffer differ in exactly three things -- which
    // hook finds the record, which table describes the fields, and how many
    // rows that table has -- and those are data, not control flow. Sixteen
    // copies of the same eight lines is what they were, and gcc cannot merge
    // them: the copies name different globals, and no optimiser invents a
    // table to index them with.
    case MC_DECL:   f_ = 0; goto fld_rd1;
    case MC_INSTR:  f_ = 1; goto fld_rd1;
    case MC_BUF:    f_ = 2; goto fld_rd1;
    case MC_VIEW:   f_ = 3; goto fld_rd1;
    case MC_DECL2:  f_ = 0; goto fld_rd2;
    case MC_INSTR2: f_ = 1; goto fld_rd2;
    case MC_BUF2:   f_ = 2; goto fld_rd2;
    case MC_VIEW2:  f_ = 3; goto fld_rd2;
    case MC_DECLS:  f_ = 0; goto fld_wr1;
    case MC_INSTRS: f_ = 1; goto fld_wr1;
    case MC_BUFS:   f_ = 2; goto fld_wr1;
    case MC_VIEWS:  f_ = 3; goto fld_wr1;
    case MC_DECLS2: f_ = 0; goto fld_wr2;
    case MC_INSTRS2:f_ = 1; goto fld_wr2;
    case MC_BUFS2:  f_ = 2; goto fld_wr2;
    case MC_VIEWS2: f_ = 3; goto fld_wr2;

    // INDEXED: the family and the field are immediates, the ELEMENT index is
    // on the stack. One pair of bodies for all four families, the same way the
    // plain ones share theirs.
    case MC_FLDI: {
	const void* rp;
	uint8_t k_;
	FETCH8(f_);
	FETCH8(b);
	NEED(2);
	if (f_ > 3) goto e_opcode;
	if ((b >= fam_n(f_)) || (b < fam_nd(f_))) goto e_opcode;
	k_ = (uint8_t)sp[0];
	sp += 1;
	if ((rp = fam_rd(vm, f_, sp[0])) == NULL) goto e_leaf_or_bounds;
	sp[0] = mc_field_get_k(rp, fam_row(f_, b), k_);
	break;
    }
    case MC_FLDIS: {
	void* wp;
	uint8_t k_;
	FETCH8(f_);
	FETCH8(b);
	NEED(3);
	if (f_ > 3) goto e_opcode;
	if ((b >= fam_n(f_)) || (b < fam_nd(f_))) goto e_opcode;
	k_ = (uint8_t)sp[0];
	if ((wp = fam_wr(vm, f_, sp[1])) == NULL) goto e_leaf_or_bounds;
	mc_field_set_k(wp, fam_row(f_, b), sp[2], k_);
	sp += 3;
	break;
    }
    fld_rd1: {
	const void* rp;
	FETCH8(b);
	NEED(1);
	// A slot below NDOUBLE is half of a field wider than a cell, and this
	// is the opcode that hands back one cell.
	if ((b >= fam_n(f_)) || (b < fam_nd(f_))) goto e_opcode;
	if ((rp = fam_rd(vm, f_, sp[0])) == NULL) goto e_leaf_or_bounds;
	sp[0] = mc_field_get(rp, fam_row(f_, b));
	break;
    }
    fld_rd2: {
	const void* rp;
	FETCH8(b);
	NEED(1);
	if ((b >= fam_nd(f_)) || ((b & 1) != 0)) goto e_opcode;
	if ((rp = fam_rd(vm, f_, sp[0])) == NULL) goto e_leaf_or_bounds;
	sp[0] = mc_field_get(rp, fam_row(f_, b));
	GOTO_PUSH_TMP(mc_field_get(rp, fam_row(f_, (uint8_t)(b + 1))));
    }
    fld_wr1: {
	void* wp;
	FETCH8(b);
	NEED(2);
	if ((b >= fam_n(f_)) || (b < fam_nd(f_))) goto e_opcode;
	if ((wp = fam_wr(vm, f_, sp[0])) == NULL) goto e_leaf_or_bounds;
	mc_field_set(wp, fam_row(f_, b), sp[1]);
	sp += 2;
	break;
    }
    fld_wr2: {
	void* wp;
	FETCH8(b);
	NEED(3);
	if ((b >= fam_nd(f_)) || ((b & 1) != 0)) goto e_opcode;
	if ((wp = fam_wr(vm, f_, sp[0])) == NULL) goto e_leaf_or_bounds;
	mc_field_set(wp, fam_row(f_, b), sp[2]);          // low cell
	mc_field_set(wp, fam_row(f_, (uint8_t)(b + 1)), sp[1]);
	sp += 3;
	break;
    }
    // ---- DOUBLES ---------------------------------------------------------
    //
    // Two cells, MOST SIGNIFICANT ON TOP: sp[0] is the high half, sp[1] the
    // low. That is Forth's order, and it is the one that makes a carry read the
    // way the textbook writes it.
    //
    // These are separate opcodes and not MC_DECL widening itself on a wide
    // field: the stack effect has to be readable from the bytecode rather than
    // from whatever utils/layout.terms happens to say.
    case MC_DLIT:
	FETCH8(b);
	a = (mc_cell_t)b;
	FETCH8(b);
	PUSH((mc_cell_t)(a | ((mc_cell_t)b << 8)));      // low
	FETCH8(b);
	a = (mc_cell_t)b;
	FETCH8(b);
	GOTO_PUSH_TMP(((mc_cell_t)(a | ((mc_cell_t)b << 8)))); // high
    case MC_DDROP:
	NEED(2);
	sp += 2;
	break;
    case MC_DADD: {
	uint32_t x, y;
	NEED(4);
	y = (uint32_t)sp[1] | ((uint32_t)sp[0] << 16);
	x = (uint32_t)sp[3] | ((uint32_t)sp[2] << 16);
	x += y;
	sp += 2;
	sp[1] = (mc_cell_t)x;
	sp[0] = (mc_cell_t)(x >> 16);
	break;
    }
    case MC_DSUB: {
	uint32_t x, y;
	NEED(4);
	y = (uint32_t)sp[1] | ((uint32_t)sp[0] << 16);
	x = (uint32_t)sp[3] | ((uint32_t)sp[2] << 16);
	x -= y;
	sp += 2;
	sp[1] = (mc_cell_t)x;
	sp[0] = (mc_cell_t)(x >> 16);
	break;
    }
    case MC_DEQ: {
	uint32_t x, y;
	NEED(4);
	y = (uint32_t)sp[1] | ((uint32_t)sp[0] << 16);
	x = (uint32_t)sp[3] | ((uint32_t)sp[2] << 16);
	sp += 3;
	sp[0] = (mc_cell_t)(x == y);
	break;
    }
    case MC_DLT: {
	uint32_t x, y;
	NEED(4);
	y = (uint32_t)sp[1] | ((uint32_t)sp[0] << 16);
	x = (uint32_t)sp[3] | ((uint32_t)sp[2] << 16);
	sp += 3;
	sp[0] = (mc_cell_t)(x < y);
	break;
    }
    case MC_ST:
	FETCH8(b);
	if (mc_state_hook == NULL) goto e_leaf;
	GOTO_PUSH_TMP(mc_state_hook(vm->ctx, b));
    case MC_NATIVE:
	FETCH8(b);
	NEED(1);
	if ((vm->leaf == NULL) || (b >= vm->nleaf)) goto e_leaf;
	// The leaf TABLE is in flash as well, so the pointer comes out of it
	// with ro_ptr before it can be called.
	MARK();
	sp[0] = ((mc_leaf_t)ro_ptr(&vm->leaf[b]))(vm->ctx, sp[0]);
	break;
    case MC_LGET:
	FETCH8(b);
	if ((vm->lv == NULL) || ((uint16_t)lvb + b >= vm->lv_size))
	    goto e_bounds;
	GOTO_PUSH_TMP(vm->lv[lvb + b]);
    case MC_LSET:
	FETCH8(b);
	if ((vm->lv == NULL) || ((uint16_t)lvb + b >= vm->lv_size))
	    goto e_bounds;
	POP(a);
	vm->lv[lvb + b] = a;
	break;

    // ---------------------------------------------------------------- writes
    case MC_STS:
	FETCH8(b);
	NEED(1);
	if (mc_state_set == NULL) goto e_leaf;
	mc_state_set(vm->ctx, b, sp[0]);
	sp += 1;
	break;
    case MC_SLT:
	NEED(2);
	sp[1] = (mc_cell_t)((int16_t)sp[1] < (int16_t)sp[0]);
	sp += 1;
	break;
    case MC_AGET:
	FETCH8(b);
	NEED(1);
	if (mc_array_hook == NULL) goto e_leaf;
	sp[0] = mc_array_hook(vm->ctx, b, sp[0]);
	break;
    case MC_ASET:
	FETCH8(b);
	NEED(2);
	if (mc_array_set == NULL) goto e_leaf;
	mc_array_set(vm->ctx, b, sp[0], sp[1]);
	sp += 2;
	break;
    case MC_NATIVEN: {
	mc_cell_t* ns;
	FETCH8(b);
	if ((vm->leafn == NULL) || (b >= vm->nleafn)) goto e_leaf;
	MARK();
	ns = ((mc_leafn_t)ro_ptr(&vm->leafn[b]))(vm->ctx, sp);
	// The leaf decides its own arity, so nothing here knows what it
	// SHOULD have left -- but a stack pointer outside the array is a
	// bug in the leaf, and a silent one everywhere else.
	if ((ns < DS_BASE) || (ns > DS_TOP)) goto e_stack;
	sp = ns;
	break;
    }
    default: goto e_opcode;
    }
    goto next;
lbl_push_tmp:
    if (sp <= DS_BASE) goto e_stack;
    *--sp = tmp;
    goto next;


lbl_result:
    if (result != NULL)
	*result = sp[0];
    return MC_OK;
e_opcode: return MC_E_OPCODE;
e_bounds: return MC_E_BOUNDS;
e_leaf:   return MC_E_LEAF;
// A family hook that answered NULL: either none was installed or the index
// named nothing. Both are the caller asking for a record that is not there.
e_leaf_or_bounds: return MC_E_BOUNDS;
e_stack:  return MC_E_STACK;
}
