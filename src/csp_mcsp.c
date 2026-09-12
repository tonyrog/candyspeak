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
const mc_field_t mc_decl_fields[] = CSP_DECL_ALL_FIELDS;
const uint8_t    mc_decl_nfield = (uint8_t)MFA_NFIELD;

// The record is read a BYTE AT A TIME into a uint32_t rather than cast to one.
// A csp_decl_t is PACKED and may sit at any address in the pool, and a 32-bit
// load from an odd address is a fault on the ARM ports -- which is how a packed
// function-pointer table once turned into a HardFault. memcpy says the same
// thing and compiles to the same code where the alignment is known.
mc_cell_t mc_field_get(const void* rec, const mc_field_t* f)
{
    const uint8_t* p = (const uint8_t*)rec + ((size_t)f->word * 4);
    uint32_t w;
    uint32_t mask;

    w = (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
	((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    // (1u << 32) is undefined, and `bits` is allowed to be 32 for a whole-word
    // field even though only the low 16 survive the cell.
    mask = (f->bits >= 32) ? 0xFFFFFFFFu
	                   : (uint32_t)(((uint32_t)1 << f->bits) - 1u);
    return (mc_cell_t)((w >> f->shift) & mask);
}

// Hooks the domain words reach the runtime through. Indirection rather than a
// direct call to csp_decl_ref so tests/mcsp.c can exercise the machine without
// linking the whole runtime behind it -- the field table is the part that can
// be quietly wrong, and it deserves a test with nothing else in the way.
const void* (*mc_decl_hook)(void* ctx, mc_cell_t i);
mc_cell_t   (*mc_nd_hook)(void* ctx);

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
	if (sp <= DS_BASE) return MC_E_STACK;		\
	*--sp = v_;					\
    } while (0)

#define POP(lv)  do {					\
	if (sp >= DS_TOP) return MC_E_STACK;		\
	(lv) = *sp++;					\
    } while (0)

#define NEED(n)  do {					\
	if ((DS_TOP - sp) < (n)) return MC_E_STACK;	\
    } while (0)

#define FETCH8(lv) do {					\
	if (ip >= vm->code_len) return MC_E_BOUNDS;	\
	(lv) = vm->code[ip++];				\
    } while (0)

int csp_mcsp_run(mc_vm_t* vm, uint16_t entry, mc_cell_t* result)
{
    uint16_t ip = entry;
    mc_cell_t* sp = vm->ds + vm->ds_size;   // empty
    uint8_t  rp = 0;
    uint8_t  lvb = 0;                       // base of the current local frame
    uint8_t  op;
    uint8_t  b;
    mc_cell_t a;

    for (b = 0; b < vm->nargs; b++)
	PUSH(vm->arg[b]);

    for (;;) {
	FETCH8(op);
	switch (op) {
	case MC_BYE:
	    NEED(1);
	    if (result != NULL)
		*result = sp[0];
	    return MC_OK;
	case MC_LIT8:
	    FETCH8(b);
	    PUSH((mc_cell_t)b);
	    break;
	case MC_LIT16:
	    FETCH8(b);
	    a = (mc_cell_t)b;
	    FETCH8(b);
	    PUSH((mc_cell_t)(a | ((mc_cell_t)b << 8)));
	    break;
	case MC_DROP:
	    POP(a);
	    break;
	case MC_DUP:
	    NEED(1);
	    PUSH(sp[0]);
	    break;
	case MC_SWAP:
	    NEED(2);
	    a = sp[0]; sp[0] = sp[1]; sp[1] = a;
	    break;
	case MC_OVER:
	    NEED(2);
	    PUSH(sp[1]);
	    break;
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
	case MC_JMP:
	    FETCH8(b);
	    ip = (uint16_t)(ip + (int8_t)b);
	    break;
	case MC_JZ:
	    FETCH8(b);
	    POP(a);
	    if (a == 0)
		ip = (uint16_t)(ip + (int8_t)b);
	    break;
	case MC_CALL: {
	    uint8_t frame;
	    FETCH8(b);
	    a = (mc_cell_t)b;
	    FETCH8(b);
	    FETCH8(frame);
	    // Two entries per call: the return address and the frame base. A
	    // return stack sized for N calls therefore holds N/2 of them, which
	    // is worth knowing when a port picks the number.
	    if ((uint8_t)(rp + 2) > vm->rs_size) return MC_E_STACK;
	    vm->rs[rp++] = ip;
	    vm->rs[rp++] = lvb;
	    if ((uint16_t)lvb + frame > vm->lv_size) return MC_E_BOUNDS;
	    lvb = (uint8_t)(lvb + frame);
	    ip = (uint16_t)(a | ((uint16_t)b << 8));
	    break;
	}
	case MC_EXIT:
	    // An empty return stack means this is the word the run started in,
	    // so its return IS the run's result. A word that ended with BYE
	    // instead could never be called from another word.
	    if (rp == 0) {
		NEED(1);
		if (result != NULL)
		    *result = sp[0];
		return MC_OK;
	    }
	    if (rp < 2) return MC_E_STACK;
	    lvb = (uint8_t)vm->rs[--rp];
	    ip = vm->rs[--rp];
	    break;
	case MC_TOR:
	    POP(a);
	    if (rp >= vm->rs_size) return MC_E_STACK;
	    vm->rs[rp++] = a;
	    break;
	case MC_RFROM:
	    if (rp == 0) return MC_E_STACK;
	    PUSH((mc_cell_t)vm->rs[--rp]);
	    break;
	case MC_RAT:
	    if (rp == 0) return MC_E_STACK;
	    PUSH((mc_cell_t)vm->rs[rp - 1]);
	    break;
	case MC_DECL: {
	    const void* d;
	    FETCH8(b);
	    NEED(1);
	    if (b >= mc_decl_nfield) return MC_E_OPCODE;
	    // A cell is 16 bits; cn.init is 32 and tm.period 28. Refusing is the
	    // point -- a truncation here would be a plausible wrong number.
	    if (mc_decl_fields[b].bits > 16) return MC_E_OPCODE;
	    if (mc_decl_hook == NULL) return MC_E_LEAF;
	    d = mc_decl_hook(vm->ctx, sp[0]);
	    if (d == NULL) return MC_E_BOUNDS;
	    sp[0] = mc_field_get(d, &mc_decl_fields[b]);
	    break;
	}
	case MC_ND:
	    if (mc_nd_hook == NULL) return MC_E_LEAF;
	    PUSH(mc_nd_hook(vm->ctx));
	    break;
	case MC_NATIVE:
	    FETCH8(b);
	    NEED(1);
	    if ((vm->leaf == NULL) || (b >= vm->nleaf)) return MC_E_LEAF;
	    sp[0] = vm->leaf[b](vm->ctx, sp[0]);
	    break;
	case MC_LGET:
	    FETCH8(b);
	    if ((vm->lv == NULL) || ((uint16_t)lvb + b >= vm->lv_size))
		return MC_E_BOUNDS;
	    PUSH(vm->lv[lvb + b]);
	    break;
	case MC_LSET:
	    FETCH8(b);
	    if ((vm->lv == NULL) || ((uint16_t)lvb + b >= vm->lv_size))
		return MC_E_BOUNDS;
	    POP(a);
	    vm->lv[lvb + b] = a;
	    break;
	case MC_NATIVEN: {
	    mc_cell_t* ns;
	    FETCH8(b);
	    if ((vm->leafn == NULL) || (b >= vm->nleafn)) return MC_E_LEAF;
	    ns = vm->leafn[b](vm->ctx, sp);
	    // The leaf decides its own arity, so nothing here knows what it
	    // SHOULD have left -- but a stack pointer outside the array is a
	    // bug in the leaf, and a silent one everywhere else.
	    if ((ns < DS_BASE) || (ns > DS_TOP)) return MC_E_STACK;
	    sp = ns;
	    break;
	}
	// MC_INSTR and MC_NN are reserved, not built: the decl field sites in
	// the tree are what this vocabulary is for, and an opcode that is
	// always an error is worse than one that is not there. Their NUMBERS
	// are held so the AVR jump table does not renumber when they arrive.
	default:
	    return MC_E_OPCODE;
	}
    }
}
