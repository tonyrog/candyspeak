// CandySpeak compiler -- tokenizer, expression compiler, declaration parsers
// and the instruction emitter. Everything between source text and instructions.
//
// Split out of csp_rt.c so the boundary is a file rather than a convention. It
// is also the boundary CSP_EXEC_ONLY draws: a node that runs a ROM image (and
// EEPROM patches, which are stored already compiled) never parses text, so on
// such a build --gc-sections drops this whole translation unit. Measured on
// mega: 102 546 -> 38 542 bytes.
//
// The parse cursors it works through live in st->cs (csp_cstate_t) rather than
// loose in csp_rt_t, so it is visible when a runtime path reaches into parse
// state -- which is a bug: st->cs.sx moves to a module's own State between
// #module and #end, so anything running on a CYCLE must read st->gsx instead.
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "csp.h"
#include "csp_strings.h"   // shared RODATA strings (generated from strings.tab)
#include "csp_parse.h"
#include "csp_print.h"
#include "csp_compile.h"
#include "csp_tok.h"
// Capture structs and the pat_* names, generated from utils/syntax.terms. The
// bytes themselves live in csp_parse.c, in one array these index into; the
// parse functions below stay here. What moved out is the data they match with,
// whose byte lengths and stop-set ids are exactly what nobody can verify by
// reading.
#include "csp_patterns.h"

// An exec-only build has no compiler: a ROM image is produced on the host and
// an EEPROM patch is stored already compiled, so nothing on the target turns
// text into instructions. Guarded as a WHOLE rather than per-function -- the
// Arduino build compiles every .c in the sketch directory, and an empty
// translation unit is how you opt out there. It also means a call that should
// not exist becomes a LINK ERROR instead of a jump into unscanned pattern
// tables, which is exactly how ./csp-exec used to segfault on a source file.
#if !defined(CSP_EXEC_ONLY)
#ifdef DEBUG
#include "csp_dump.h"
#include <stdio.h>
extern int debug;
#endif

#ifdef DEBUG
extern void print_rentry(csp_rt_t* st, char* name, rentry_t* rp);

void print_rentry(csp_rt_t* st, char* name, rentry_t* rp)
{
    DBG("%s={", name);
    if (rp->X) DBG("name=%s,", decl_name(st, rp->ix));
    DBG("flags=");
    if (rp->I) DBG("im ");
    if (rp->L) DBG("ld ");
    if (rp->X) DBG("ix ");
    DBG(",vt=%s", (char*)csp_fmt_vtype(rp->vt));
    if (rp->L) DBG(",reg=%d", rp->reg);
    if (rp->X) DBG(",ix=0x%04x", rp->ix);
    if (rp->I) { DBG(",val="); csp_print_value(st, rp->vt, rp->val); }
    DBG("}");
}
#endif

NOINLINE static csp_instr_t* alloc_instr_ptr(csp_rt_t* st,int* pos,opcode_t op)
{
    int i;                        // logical instr index (or the dummy slot)
    csp_instr_t* ip;
    if (st->cs.ap == NULL) {
	i = MAX_INSTRS;           // scratch index (never fetched: ev folds inline)
	ip = &st->imm_scratch;    // write-only dummy for immediate `> expr` eval
    }
    else if ((st->ps.nn - st->rom_nn) >= MAX_INSTRS ||   // index cap, or
	     !mem_fits(st, sizeof(csp_instr_t))) {       // arena byte budget
	csp_set_error(st, ERR_TOO_MANY_INSTRUCTIONS);
	return NULL;
    }
    else {
	i = st->ps.nn++;
	ip = ram_instr_at(st, i);
	// Mirror number_rules: a rule body ends at NEXT/ENTER, so each one opens
	// the next body. The implicit body at the range base is the initial 1.
	if ((op == OP_NEXT) || (op == OP_ENTER))
	    st->n_rule_emit++;
    }
    ip->op = op;
    if (pos != NULL) *pos = i;
    return ip;
}

NOINLINE bool_t asm_RULE(csp_rt_t* st, int* pos, reg_t cnd, int nxt)
{
    csp_instr_t* ip = alloc_instr_ptr(st, pos, OP_RULE);
    if (ip != NULL) {
	ip->r.cnd = cnd;
	ip->r.nxt = nxt;
	ip->r.implicit = st->cs.rule_implicit;   // set for bare NORMAL+ rules
	return 1;
    }
    return 0;
}

NOINLINE static bool_t asm_NEXT(csp_rt_t* st, int r)
{
    csp_instr_t* ip = alloc_instr_ptr(st, NULL, OP_NEXT);
    if (ip != NULL) {
	ip->x.x = r;
	return 1;
    }
    return 0;
}

// #in <state> block gate: reg x holds the current State (loaded just before);
// nxt is patched at #end to the distance skipping the whole block. implicit marks
// the auto NORMAL+ wrap so the listing renders the rule bare (see csp_print.c).
NOINLINE static bool_t asm_INSTATE(csp_rt_t* st, int* pos, reg_t x, int imm,
				   int implicit)
{
    csp_instr_t* ip = alloc_instr_ptr(st, pos, OP_INSTATE);
    if (ip != NULL) {
	ip->in.x = x;
	ip->in.imm = imm;
	ip->in.nxt = 0;   // patched at #end
	ip->in.implicit = implicit ? 1 : 0;
	return 1;
    }
    return 0;
}

// OR-chain gate for `#in A B C`: if State == imm, jump INTO the block. nxt is the
// forward distance to the block's first rule, patched once the chain is complete
// (csp_parse_in). The final state of the list uses asm_INSTATE (skip-if-!=), so
// the chain reads "enter if A, else enter if B, else enter if C, else skip".
NOINLINE static bool_t asm_NINSTATE(csp_rt_t* st, int* pos, reg_t x, int imm)
{
    csp_instr_t* ip = alloc_instr_ptr(st, pos, OP_NINSTATE);
    if (ip != NULL) {
	ip->in.x = x;
	ip->in.imm = imm;
	ip->in.nxt = 0;   // patched once the block's first instruction is known
	ip->in.implicit = 0;
	return 1;
    }
    return 0;
}

// Defined near csp_parse_in; used earlier by csp_parse_end and csp_parse_rule.
NOINLINE static bool_t open_in_block(csp_rt_t* st, const uint8_t* states, int ns,
				     int implicit);
NOINLINE static void close_in_block(csp_rt_t* st);

// Narrow a compiler xindex_t to the index_t a memory instruction carries, and
// emit the OP_SETO in front of it when the reference names an object.
//
// This is the ONLY place the two representations meet. An xindex_t knows which
// object it means; an index_t has a single selector bit and can only say "global"
// or "the object running". Every instruction with a mem field goes through
// asm_mem_part or asm_memi, which is what makes one funnel enough.
//
// OP_SETO is emitted IMMEDIATELY before its access and is consumed by it, so the
// two are always adjacent and there is no window for a jump to land between them.
// Can `X[i]` mean anything? Every type an array can be made of -- a subscript on
// anything else is either the buffer byte-view (handled before this is asked) or
// a mistake. One predicate so the read side and the write side, which are
// separate paths through the parser, cannot drift apart on which types they take.
static int is_subscriptable(decl_t t)
{
    return (t == DECL_VARIABLE) || (t == DECL_CONSTANT) ||
	   (t == DECL_ANALOG)   || (t == DECL_DIGITAL);
}

NOINLINE static bool_t asm_seto(csp_rt_t* st, xindex_t mem, index_t* out)
{
    unsigned m = XOBJ(mem);

    // A pending `A[expr]` subscript, consumed here for the same reason OP_SETO
    // is emitted here: the base-setting instruction has to sit IMMEDIATELY in
    // front of its access, and this is the one funnel every mem instruction
    // passes through. The access must read CURRENT-relative for the base to
    // apply, whatever the reference's own selector said.
    if (st->cs.arr_len > 0) {
	csp_instr_t* ip;
	uint8_t  r = st->cs.arr_reg;
	uint16_t l = st->cs.arr_len;
	st->cs.arr_len = 0;                // one-shot: cleared before we can fail
	*out = MAKE_INDEX(CURRENT, XIDX(mem));
	if ((ip = alloc_instr_ptr(st, NULL, OP_SETOX)) == NULL)
	    return 0;
	ip->ox.x      = r;
	ip->ox.len    = l;
	ip->ox.stride = 1;                 // scalar array; one decl per element
	return 1;
    }
    *out = MAKE_INDEX(m ? CURRENT : GLOBAL, XIDX(mem));
    if ((m == XOBJ_GLOBAL) || (m == XOBJ_CURRENT))
	return 1;
    {
	csp_instr_t* ip = alloc_instr_ptr(st, NULL, OP_SETO);
	if (ip == NULL)
	    return 0;
	ip->o.obj = m;
    }
    return 1;
}


// Immediate mode (`> expr` at the prompt, and the ev fold) EXECUTES instead of
// emitting, so there is no instruction stream to put an OP_SETO into. Bind the
// object exactly as OP_SETO would, run the value op, put the context back.
typedef struct { uint8_t cur; index_t cbase; } ctx_save_t;

static index_t ctx_enter(csp_rt_t* st, xindex_t ix, ctx_save_t* sv)
{
    unsigned m = XOBJ(ix);
    sv->cur   = st->cur;
    sv->cbase = st->cbase;
    if ((m != XOBJ_GLOBAL) && (m != XOBJ_CURRENT))
	csp_ctx_set(st, m);
    return MAKE_INDEX(m ? CURRENT : GLOBAL, XIDX(ix));
}

static void ctx_leave(csp_rt_t* st, const ctx_save_t* sv)
{
    st->cur   = sv->cur;
    st->cbase = sv->cbase;
}

NOINLINE static bool_t asm_mem_part(csp_rt_t* st, opcode_t op, reg_t x,
				 xindex_t mem, csp_part_t part)
{
    csp_instr_t* ip;
    index_t m;
    if (!asm_seto(st, mem, &m))
	return 0;
    ip = alloc_instr_ptr(st, NULL, op);
    if (ip != NULL) {
	ip->m.y = part;
	ip->m.x = x;
	ip->m.mem = m;
	return 1;
    }
    return 0;
}

NOINLINE static bool_t asm_memi(csp_rt_t* st, opcode_t op, reg_t x,
				xindex_t mem, int8_t imm)
{
    csp_instr_t* ip;
    index_t m;
    if (!asm_seto(st, mem, &m))
	return 0;
    ip = alloc_instr_ptr(st, NULL, op);
    if (ip != NULL) {
	ip->mi.x = x;
	ip->mi.imm = imm;
	ip->mi.mem = m;
	return 1;
    }
    return 0;
}

// Unused since State gating left the rule body (rule_state / dispatch). Kept for
// the planned `<var> == <tiny const>` -> OP_EQI peephole. OP_EQI itself is still
// evaluated, listed and graphed -- just no longer emitted.
#if 0
NOINLINE static bool_t asm_EQI(csp_rt_t* st, reg_t x, index_t mem, int8_t imm)
{
    return asm_memi(st, OP_EQI, x, mem, imm);
}
#endif

// STI: store a small immediate to memory in one instruction (mirror of EQI).
// x is the register the rule's NEXT points at -- STI never writes it at runtime,
// so it stays a dead slot used only to render the rule body in disassembly.
NOINLINE static bool_t asm_STI(csp_rt_t* st, reg_t x, xindex_t mem, int8_t imm)
{
    return asm_memi(st, OP_STI, x, mem, imm);
}

// A plain (non-part, non-reactive) store of a small signed-8-bit integer
// immediate can collapse LI+ST into a single STI. Keeps state assignments and
// other small constants to one instruction and one disassembly hit.
static int fits_sti(opcode_t op, const rentry_t* r)
{
    return (op == OP_ST) && r->I && (r->vt != V_FLOAT) &&
	(r->val.i >= TINY_MIN) && (r->val.i <= TINY_MAX);
}

NOINLINE static bool_t asm_mem(csp_rt_t* st, opcode_t op, reg_t x, xindex_t mem)
{
    return asm_mem_part(st, op, x, mem, PART_VAL);
}

NOINLINE static bool_t asm_imm(csp_rt_t* st, opcode_t op, reg_t x, int16_t imm)
{
    csp_instr_t* ip = alloc_instr_ptr(st, NULL, op);
    if (ip != NULL) {    
	ip->i.x = x;
	ip->i.imm = imm;
	return 1;
    }
    return 0;
}

NOINLINE static bool_t asm_LI(csp_rt_t* st, reg_t x, int16_t imm)
{
    return asm_imm(st, OP_LI, x, imm);
}

NOINLINE static bool_t asm_LIU(csp_rt_t* st, reg_t x, uint16_t imm)
{
    return asm_imm(st, OP_LIU, x, (int16_t)imm);
}

NOINLINE static bool_t asm_LIH(csp_rt_t* st, reg_t x, uint16_t imm)
{
    return asm_imm(st, OP_LIH, x, (int16_t)imm);
}


// Smart load: choose LI, LIU, or LIU+LIH based on value
NOINLINE static bool_t csp_load_int(csp_rt_t* st, reg_t x, ivalue_t val)
{
    if ((val >= -32768) && (val <= 32767)) {
	return asm_LI(st, x, (int16_t)val);
    }
    else {
	uint32_t uval = (uint32_t)val;
	if (!asm_LIU(st, x, (uint16_t)(uval & 0xFFFF)))
	    return 0;
	if (uval > 0xFFFF) {
	    if (!asm_LIH(st, x, (uint16_t)(uval >> 16)))
		return 0;
	}
	return 1;
    }
    return 0;
}

NOINLINE static bool_t csp_load_uint(csp_rt_t* st, reg_t x, uvalue_t val)
{
    if (val <= 32767) {
	return asm_LI(st, x, (int16_t)val);
    }
    else if (val <= 0xFFFF) {
	return asm_LIU(st, x, (uint16_t)val);
    }
    else {
	if (!asm_LIU(st, x, (uint16_t)(val & 0xFFFF)))
	    return 0;
	return asm_LIH(st, x, (uint16_t)(val >> 16));
    }
}

NOINLINE static bool_t csp_load_float(csp_rt_t* st, reg_t x, fvalue_t val)
{
#if FVALUE_IS_FIXPOINT
    // Fixpoint is just an int32_t, load as signed
    return csp_load_int(st, x, (ivalue_t)val);
#else
    union { float f; uint32_t u; } v;
    v.f = val;
    if (v.u == 0) {
	return asm_LI(st, x, 0);  // 0.0
    }
    if (!asm_LIU(st, x, (uint16_t)(v.u & 0xFFFF)))
	return 0;
    return asm_LIH(st, x, (uint16_t)(v.u >> 16));
#endif
}

static int asm_ARG(csp_rt_t* st, reg_t x, int16_t i)
{
    return asm_imm(st, OP_ARG, x, i);
}

NOINLINE static bool_t asm_alu(csp_rt_t* st, opcode_t op,
			       reg_t x, reg_t y, reg_t z)
{
    csp_instr_t* ip = alloc_instr_ptr(st, NULL, op);
    if (ip != NULL) {
	ip->a.x = x;
	ip->a.y = y;
	ip->a.z = z;
	return 1;
    }
    return 0;
}

NOINLINE static bool_t asm_CALL(csp_rt_t* st, reg_t x, int func_idx, int is_user, uint16_t argcode)
{
    csp_instr_t* ip = alloc_instr_ptr(st, NULL, OP_CALL);    
    if (ip != NULL) {
	ip->f.x   = x;
	ip->f.idx = func_idx;
	ip->f.usr = is_user;
	ip->f.avt = argcode;
	return 1;
    }
    return 0;
}

NOINLINE static bool_t asm_ENTER(csp_rt_t* st, int* pos, int n, index_t mx)
{
    csp_instr_t* ip = alloc_instr_ptr(st, pos, OP_ENTER);        
    if (ip != NULL) {
	ip->e.num = n;
	ip->e.mx = mx;
	return 1;
    }
    return 0;
}

NOINLINE static bool_t asm_LEAVE(csp_rt_t* st, int* pos, int n, index_t mx)
{
    csp_instr_t* ip = alloc_instr_ptr(st, pos, OP_LEAVE);
    if (ip != NULL) {
	ip->v.num = n;
	ip->v.mx = mx;
	return 1;
    }
    return 0;
}

NOINLINE static bool_t asm_NEW(csp_rt_t* st, unsigned ent, index_t obj)
{
    csp_instr_t* ip = alloc_instr_ptr(st, NULL, OP_NEW);
    if (ip != NULL) {
	ip->n.ent = ent;
	ip->n.obj = obj;
	return 1;
    }
    return 0;
}

static bool_t asm_bop(csp_rt_t* st, opcode_t op, index_t x ,index_t y, index_t z)
{
    return asm_alu(st, op, x, y, z);
}

static bool_t asm_uop(csp_rt_t* st, opcode_t op, index_t x, index_t y)
{
    return asm_alu(st, op, x, y, 0);
}

static bool_t asm_CVTIF(csp_rt_t* st, index_t x, index_t y)
{
    return asm_uop(st, OP_CVTIF, x, y);
}

static bool_t asm_CVTFI(csp_rt_t* st, index_t x, index_t y)
{
    return asm_uop(st, OP_CVTFI, x, y);
}

static bool_t asm_MOV(csp_rt_t* st, reg_t x, reg_t y)
{
    return asm_uop(st, OP_MOV, x, y);
}

static bool_t asm_AND(csp_rt_t* st, reg_t x, reg_t y, reg_t z)
{
    return asm_bop(st, OP_AND, x, y, z);
}

#if 0
// Unused since State gating moved out of the rule body (rule_state / dispatch).
// Kept for the planned `<var> == <tiny const>` -> EQI peephole, which will want
// these register-form compares again.
static bool_t asm_EQEQ(csp_rt_t* st, reg_t x, reg_t y, reg_t z)
{
    return asm_bop(st, OP_EQEQ, x, y, z);
}
static bool_t asm_OR(csp_rt_t* st, reg_t x, reg_t y, reg_t z)
{
    return asm_bop(st, OP_OR, x, y, z);
}
NOINLINE static bool_t asm_NOP(csp_rt_t* st)
{
    return (alloc_instr_ptr(st, NULL, OP_NOP) != NULL);
}
#endif

// Map a name after '.' to a part selector, PART_LAST if it is not a part.
// Parts are ordinary words disambiguated by position (obj.field wins in code).
NOINLINE csp_part_t part_from_tstr(const tstr_t* s)
{
    switch(s->len) {
    case 2:
	if (ro_memcmp(s->ptr, s_id, 2) == 0)     return PART_ID;
	if (ro_memcmp(s->ptr, s_rx, 2) == 0)     return PART_RX;
	if (ro_memcmp(s->ptr, s_tx, 2) == 0)     return PART_TX;
	break;
    case 3:
	if (ro_memcmp(s->ptr, s_pin, 3) == 0)    return PART_PIN;
	if (ro_memcmp(s->ptr, s_dir, 3) == 0)    return PART_DIR;
	if (ro_memcmp(s->ptr, s_dlc, 3) == 0)    return PART_DLC;
	if (ro_memcmp(s->ptr, s_len, 3) == 0)    return PART_LEN;
	break;
    case 4:
	if (ro_memcmp(s->ptr, s_port, 4) == 0)   return PART_PORT;
	break;
    case 5:
	if (ro_memcmp(s->ptr, s_value, 5) == 0)  return PART_VAL;
	if (ro_memcmp(s->ptr, s_fired, 5) == 0)  return PART_FIRED;
	break;
    case 6:
	if (ro_memcmp(s->ptr, s_endian, 6) == 0) return PART_ENDIAN;
	if (ro_memcmp(s->ptr, s_period, 6) == 0) return PART_PERIOD;
	break;
    default:
	break;
    }
    return PART_LAST;
}


// Returns the RODATA name as-is. It used to hand back a tstr_t pointing at the
// same flash bytes, which csp_set_err_arg_tstr then memcpy'd -- the wrong
// address space on AVR. Flash names go through csp_set_err_arg_rostr instead.
static rostring_t decl_type_name(decl_t type)
{
    switch(type) {
    case DECL_VARIABLE: return ros_variable;
    case DECL_CONSTANT: return ros_constant;
    case DECL_MODULE:   return ros_module;
    case DECL_END:      return ros_end;
    case DECL_OBJECT:   return ros_object;
    case DECL_TIMER:    return ros_timer;
    case DECL_DIGITAL:  return ros_digital;
    case DECL_ANALOG:   return ros_analog;
    case DECL_FIELD:    return ros_field;
    case DECL_BUFFER:   return ros_buffer;
    case DECL_STATES:   return ros_states;	
    default:            return ros_undefined;
    }
}

// Symbol-table helpers that only the compiler uses. They lived in csp_rt.c
// because that is where everything lived; nothing in the runtime calls them.
NOINLINE index_t lookup_const(csp_rt_t* st, vtype_t vt, value_t v)
{
    index_t i;
    for (i = 0; i < st->ps.nd; i++) {
	csp_decl_t d = csp_get_decl(st, i);
	if ((d.type == DECL_CONSTANT) && (vt == d.vt)) {
	    if (decl(st,i,cn.init.u) == v.u)  // binary compare!
		return MAKE_INDEX(0,i);
	}
    }
    return BAD_INDEX;
}
NOINLINE index_t new_signed_const(csp_rt_t* st, ivalue_t v)
{
    index_t ix;
    int i;
    const tstr_t empty = { .ptr = NULL, .len = 0};
    if ((ix = csp_new_decl(st,&empty,DECL_CONSTANT,0)) == BAD_INDEX)
	return BAD_INDEX;
    i = INDEX(ix);
    ram_decl_at(st,i)->cn.init.i = v;
    return ix;
}
// Lookup for DECLARING a name: only the scope the new decl lands in. A module
// member may shadow a global -- otherwise adding a global later would break
// every module that happens to use that name.
NOINLINE index_t csp_lookup_decl_local(csp_rt_t* st, const tstr_t* name)
{
    int start = (st->cs.mdef != BAD_INDEX) ? INDEX(st->cs.mdef)+1 : 0;
    return lookup_decl_in(st, name, start, st->ps.nd);
}
// new uniq declaration
NOINLINE index_t csp_new_udecl(csp_rt_t* st, const tstr_t* name, decl_t type)
{
    index_t ix;
    
    if ((ix = csp_lookup_decl_local(st, name)) != BAD_INDEX) {
	if (csp_set_error(st, ERR_ALREADY_DEFINED)) {
	    rostring_t typ = ros_name;
	    if (decl(st,ix,type) == type)
		typ = decl_type_name(type);
	    csp_set_err_arg_rostr(st, 0, typ);
	    csp_set_err_arg_tstr(st, 1, name);
	}
	return BAD_INDEX;
    }
    return csp_new_decl(st, name, type, 0);
}
// True when decl `di` belongs to the module body currently being parsed. Used
// to decide whether a name resolves to a per-instance member (which has to be
// addressed CURRENT-relative) or to a global (which must NOT be).
NOINLINE int is_module_local(csp_rt_t* st, xindex_t di)
{
    return (st->cs.mdef != BAD_INDEX) && ((int)XIDX(di) > (int)INDEX(st->cs.mdef));
}

// The declaration keyword table. Compiler-only: nothing else looks a
// declaration name up. Rows generated from utils/syntax.terms, which also
// generates the dtok_t enum indexing them -- D_UART, D_SOCKET and D_MOD are
// enum members with no row, and stay that way so D_LAST does not move.
const op_entry_t decl_table[] RODATA = {
    CSP_DECL_TABLE
};

static int find_decl_entry(const char* name, int namelen)
{
    return find_op_entry(decl_table, sizeof(decl_table)/sizeof(decl_table[0]),
			 name, namelen);
}

// decl_table lives in RODATA. On AVR that is PROGMEM, so `decl_table[i].code`
// does NOT read the table -- it reads DATA space at the table's flash address,
// which on a mega lands inside CandySpeak's own arena. The byte found there
// changes with the program, the pool layout and every size change, so the
// dispatch worked or failed by luck. Every other reader of these tables already
// goes through ro_byte (op_table_tok, op_table_arity, ...); this one did not.
static int8_t decl_table_code(int i)
{
    return (int8_t)ro_byte(&decl_table[i].code);
}

NOINLINE static int dec(int c)
{
    if ((c >= '0') && (c <= '9'))
	return (c - '0');
    return 0;
}

NOINLINE static int hex(int c)
{
    if ((c >= '0') && (c <= '9'))
	return (c - '0');
    if ((c >= 'a') && (c <= 'f'))
	return (c - 'a')+10;
    if ((c >= 'A') && (c <= 'F'))
	return (c - 'A')+10;
    return 0;
}

#define TOK(x) do { tok = (x); goto done; } while(0)
#define SYM(x,p,l) do { \
	tok = (x); \
	val.str.ptr=(p);	    \
	val.str.len=(l); \
	goto done;	 \
    } while(0)
#define TOK_INT(y) do { tok = INT; val.val.i = (y); goto done; } while(0)
#define TOK_FLT(y) do { tok = FLT; val.val.f = (y); goto done; } while(0)

NOINLINE static int csp_next_token(csp_rt_t* st, char* str, token_t* tp)
{
    char* str0 = str;
    int c;
    int sign = 1;
    tok_t tok;
    tokval_t val;
next:
    c = *str++;
    switch(c) {
    case '\0': str--; TOK(NONE);
    case ' ':
    case '\t': goto next;
    case '\r':
	if (*str == '\n') {
	    str++; TOK(NEWLINE);
	}
	TOK(NEWLINE);
    case '\n': TOK(NEWLINE);
    case ',': TOK(COMMA);
    // '..' is ONE token. Scanned as two DOTs it had to be matched as two, and a
    // pattern cannot capture "the range appeared" from a pair -- pat_body
    // captured the first dot and matched the second, which works but means the
    // grammar cannot say `..` where it means `..`. It also cost a token slot of
    // MAX_LINE_TOKENS (24 embedded) per range.
    //
    // A float never reaches here: `1.5` is consumed inside the number scan, so
    // `1..5` is INT DOTDOT INT and `1.5` is one FLT.
    case '.':
	if (*str == '.') {
	    str++;
	    TOK(DOTDOT);
	}
	TOK(DOT);
    case ':': TOK(COLON);
    case '#': TOK(HASH);
    case '?': TOK(QUEST);
    case '*': TOK(ASTERISK);
    case '/':
	if (*str == '/') {
	    // Skip to end of line and CONSUME the terminator, the way every other
	    // token does -- `case '\n'` above is reached with the character
	    // already past. Leaving it here returned a NEWLINE token without
	    // eating the newline, so csp_parse's loop scanned the same line end a
	    // second time and bumped ps.line twice. That is why a syntax error in
	    // a commented file reported a line number one PER COMMENT too high.
	    str++;
	    while(*str) {
		if (*str == '\n') {
		    str++;
		    TOK(NEWLINE);
		}
		if (*str == '\r') {
		    str++;
		    if (*str == '\n')
			str++;
		    TOK(NEWLINE);
		}
		str++;
	    }
	    TOK(NEWLINE);
	}
	else {
	    TOK(SLASH);
	}
    case '%': TOK(PERCENT);
    case '&':
	switch(*str) {
	case '&': str++; TOK(AMPAMP);
	default: TOK(AMP);
	}
	break;
    case '|':
	switch(*str) {
	case '|': str++; TOK(BARBAR);
	default: TOK(BAR);
	}
	break;
    case '^': TOK(CIRC);
    case '~': TOK(TILDE);
    case '!':
	switch(*str) {
	case '=': str++; TOK(NEQ);
	default: TOK(EXCLAMATION);
	}
	break;
    case '=':
	switch(*str) {
	case '=': str++; TOK(EQEQ);
	default: TOK(EQ);
	}
	break;
    case '<':
	switch(*str) {
	case '=': str++; TOK(LTEQ);
	case '<': str++; TOK(LTLT);
	case '-': str++; TOK(RIMP);
	default: TOK(LT);
	}
	break;
    case '>':
	switch(*str) {
	case '=': str++; TOK(GTEQ);
	case '>': str++; TOK(GTGT);
	default: TOK(GT);
	}
	break;
    case '[': TOK(LB);
    case ']': TOK(RB);
    case '{': TOK(LBRACE);
    case '}': TOK(RBRACE);
    case '(': TOK(LP);
    case ')': TOK(RP);
    case '"': {
	char* qstr = str;  // point to first string char
	char* dst = str;
	int c;
	int len = 0;
	while((c = *str++) && (c != '"')) {
	    if (c == '\\') {
		switch((c=*str++)) {
		case 'a': c = '\a'; break;
		case 'b': c = '\b'; break;
		case 'e': c = '\e'; break;
		case 'f': c = '\f'; break;
		case 'n': c = '\n'; break;
		case 'r': c = '\r'; break;
		case 't': c = '\t'; break;
		case 'v': c = '\v'; break;
		default: break;
		}
	    }
	    *dst++ = c;
	    len++;
	}
	SYM(STR, qstr, len);
    }
    case '-':
	TOK(MINUS);
    case '+':
	TOK(PLUS);
    default:
	if (ISDIGIT(c))
	    goto number;
	else if (ISALPHA(c)) {
	    char *name = str-1;
	    int len = 1;
	    int i;
	    while (ISALPHA(*str) || ISDIGIT(*str) || (*str == '_') ) {
		str++;
		len++;
	    }
	    if (len > MAX_NAME_LEN) {
		if (csp_set_error(st, ERR_NAME_TOO_LONG)) {
		    csp_set_err_arg_int(st, 0, len);
		    // the limit as an ARG, so the text can live in strings.tab
		    // (it used to be pasted in with stringify(MAX_NAME_LEN))
		    csp_set_err_arg_int(st, 1, MAX_NAME_LEN);
		}
		return -1; // fixme set error code
	    }
	    if ((i = find_tok_entry(name,len)) >= 0)
		TOK(op_table_tok(i));
	    SYM(WORD, name, len);
	}
	return -1;
    number:
	if ((c == '0') && (*str == 'x')) {
	    // Accumulate the bit pattern in uint32 -- 0xFFFFFFFF is the idiom for
	    // an all-ones mask (== -1 as int32), and unsigned wrap is defined
	    // where signed overflow would be UB. Reject past 32 significant bits.
	    uint32_t uv = 0;
	    int ndig = 0;
	    str++;
	    while(ISXDIGIT(*str)) {
		uv = uv*16 + hex(*str++);
		if (++ndig > 8) {
		    csp_set_error(st, ERR_NUMBER_RANGE);
		    return -1;
		}
	    }
	    TOK_INT((ivalue_t)uv * sign);
	}
	if (ISDIGIT(c)) {
	    // Accumulate in uint64 so the running value never overflows during
	    // the scan (v*10 in int32 is UB past 2^31). Range is checked against
	    // the target type once the whole number -- and whether it is a float
	    // -- is known.
	    uint64_t uv = dec(c);
	    while(ISDIGIT(*str)) {
		uv = uv*10 + dec(*str++);
		if (uv > 0xffffffffULL) {  // far past any target range; stop early
		    csp_set_error(st, ERR_NUMBER_RANGE);
		    return -1;
		}
	    }
	    // parse simple fraction for now
	    if ((str[0] == '.') && ISDIGIT(str[1])) {
#if FVALUE_IS_FIXPOINT
		fvalue_t result;
		// Parse as Q16.16 fixpoint
		fvalue_t frac;
		uint32_t denom = 1;
		uint32_t numer = 0;
		ivalue_t v;
		// A number literal is always non-negative here -- a leading '-' is a
		// separate unary-minus token (runtime NEG), never folded in. Q16.16
		// thus tops out at an integer part of 32767; a larger magnitude
		// cannot be represented, so reject rather than silently wrap
		// (FIX_FROM_INT would overflow int32).
		if (uv > 32767) {
		    csp_set_error(st, ERR_NUMBER_RANGE);
		    return -1;
		}
		v = (ivalue_t)uv;
		str++;
		while(ISDIGIT(*str)) {
		    numer = numer*10 + dec(*str++);
		    denom *= 10;
		}
		frac = (int32_t)(((uint64_t)numer<<FIX_SHIFT) / denom);
		result = FIX_FROM_INT(v) + frac;
		TOK_FLT(result);
#else
		float b = 0.1;
		float f = 0.0;
		str++;
		while(ISDIGIT(*str)) {
		    f = f + (b*dec(*str++));
		    b /= 10.0;
		}
		f += (float)uv;
		TOK_FLT(f*sign);
#endif
	    }
	    // Integer literal: must fit int32 (a leading '-' is a separate token,
	    // so the literal itself is non-negative). Reject anything larger than
	    // INT32_MAX rather than emit a silently wrapped value.
	    if (uv > 2147483647ULL) {
		csp_set_error(st, ERR_NUMBER_RANGE);
		return -1;
	    }
	    TOK_INT((ivalue_t)uv * sign);
	}
	return -1;
    }
done:
    tp->t = tok; tp->v = val;
    return str - str0;
}

// scan one line of tokens
NOINLINE int csp_scan_line(csp_rt_t* st, char* str, token_t* tv, size_t* num_toks)
{
    char* str0 = str;
    size_t i;
    size_t max_toks = *num_toks;

    i = 0;
    while(i < max_toks) {
	int n = csp_next_token(st, str, &tv[i]);
	if (n < 0)
	    return -1;
	str += n;
	if ((tv[i].t == NEWLINE) || (tv[i].t == NONE)) {
	    *num_toks = i;
	    return str-str0;
	}
	i++;
    }
    csp_set_error(st, ERR_TOO_MANY_TOKENS);
    return -1;
}

void csp_pstate_save(csp_rt_t* st, csp_pmark_t* pm)
{
    pm->ps          = st->ps;
    pm->mdef        = st->cs.mdef;
    pm->ent         = st->cs.ent;
    pm->sdef        = st->cs.sdef;
    pm->in_marker   = st->cs.in_marker;
    pm->save_sx     = st->cs.save_sx;
    pm->sx          = st->cs.sx;
    pm->cur         = st->cur;
    pm->n_rule_emit = st->n_rule_emit;
}

// Rewind to a mark. Dropping nn/nd/nq/ns/strp back un-writes whatever the
// failed parse emitted: the slots are simply reused by the next parse. Only
// the cursors need explicit restoring.
void csp_pstate_restore(csp_rt_t* st, csp_pmark_t* pm)
{
    // Keep the diagnostics: err/err_args/err_strp describe why we are rewinding
    // and the caller has not necessarily reported them yet. Restoring them would
    // reset err to ERR_OK and the failure would print as "ok".
    csp_err_t err = st->ps.err;
    uintptr_t a0 = st->ps.err_args[0], a1 = st->ps.err_args[1],
	      a2 = st->ps.err_args[2];
    uint32_t esp = st->ps.err_strp;
    uint32_t line = st->ps.line;

    st->ps          = pm->ps;
    st->ps.err      = err;
    st->ps.err_args[0] = a0;
    st->ps.err_args[1] = a1;
    st->ps.err_args[2] = a2;
    st->ps.err_strp = esp;
    st->ps.line     = line;
    st->cs.mdef        = pm->mdef;
    st->cs.ent         = pm->ent;
    st->cs.sdef        = pm->sdef;
    st->cs.in_marker   = pm->in_marker;
    st->cs.save_sx     = pm->save_sx;
    st->cs.sx          = pm->sx;
    st->cur         = pm->cur;
    st->n_rule_emit = pm->n_rule_emit;
}

NOINLINE static void alloc_init(reg_allocator_t* ap)
{
    int i;
    for (i = 0; i < MAX_REGS; i++) {
	ap->free_regs[i] = i;
	ap->rmap[i] = BAD_INDEX;
    }
    ap->top = 0;
}

NOINLINE static int alloc_reg(csp_rt_t* st)
{
    reg_allocator_t* ap;
    if ((ap = st->cs.ap) != NULL) {
	int r = ap->free_regs[ap->top++];
	ap->rmap[r] = BAD_INDEX;
	return r;
    }
    return 0;
}

NOINLINE static void free_reg(csp_rt_t* st, int r)
{
    reg_allocator_t* ap;
    if ((ap = st->cs.ap) != NULL) {
	index_t ix;
	ap->free_regs[--ap->top] = r;
	if ((ix = ap->rmap[r]) != BAD_INDEX) {
	    ap->rmap[r] = BAD_INDEX;
	    if (INDEX(ix) >= st->rom_nd)   // is_mapped cache is RAM-only
		ram_decl_at(st,INDEX(ix))->is_mapped = 0;
	}
    }
}

// load immedate value.
NOINLINE static bool_t csp_load_value(csp_rt_t* st, reg_t x, vtype_t vt, value_t val)
{
    switch(vt) {
    case V_INDEX:
    case V_TIMER:
    case V_DIGITAL:
    case V_ANALOG:
    case V_FIELD:
	// indices are unsigned MAKE_INDEX values: a CURRENT-relative index can
	// exceed int16 positive range, so load unsigned (LIU) -- a signed LI
	// would store it negative and misrender on disassembly.
	return csp_load_uint(st, x, val.u);
    case V_INTEGER:
	return csp_load_int(st, x, val.i);
    case V_UNSIGNED:
	return csp_load_uint(st, x, val.u);
    case V_FLOAT:
	return csp_load_float(st, x, val.f);
    case V_STRING:
	return asm_LI(st, x, val.s);  // load string index
    default:
	return 0;
    }
}

// Add unique variable to var list (for <- parsing)
NOINLINE static void add_var(csp_rt_t* st, xindex_t ix)
{
    if (st->cs.rimp) {  // only when in RHS in expression x <- a+b+c
	int i;
	for (i = 0; i < st->cs.nvar; i++)
	    if (st->cs.var[i] == ix) return;  // already in list
	if (st->cs.nvar < MAX_VARREFS)
	    st->cs.var[st->cs.nvar++] = ix;
    }
}

// Map declaration (variable/constant/digital...)
NOINLINE int map_reg(csp_rt_t* st, xindex_t ix)
{
    reg_allocator_t* ap;
    int dst;
    // The is_mapped/reg register cache lives in the decl, so it is only usable
    // for RAM decls; a ROM decl (read-only flash) simply allocates a fresh reg.
    int rom = (XIDX(ix) < st->rom_nd);
    // A reference to a NAMED object is never cached. The cache keys on the decl,
    // which is the module TEMPLATE and is shared by every instance -- so caching
    // `safe.a` would hand its register to `other.a`. The rmap[] check used to
    // catch that, because an index carried its object number; it cannot now, and
    // widening rmap to xindex_t would cost RAM on every target to serve a case
    // that costs one extra LD to get right. See asm_seto.
    int named = (XOBJ(ix) != XOBJ_GLOBAL) && (XOBJ(ix) != XOBJ_CURRENT);

    if ((ap = st->cs.ap) != NULL) {
	// Check if already mapped AND mapping is still valid
	if (!rom && !named && decl(st,XIDX(ix),is_mapped)) {
	    reg_t r = decl(st,XIDX(ix),reg);
	    if (st->cs.ap->rmap[r] == XIDX(ix))
		return r;  // mapping still valid
	    // Stale mapping - clear it
	    ram_decl_at(st,XIDX(ix))->is_mapped = 0;
	}
	dst = alloc_reg(st);
	if (!rom && !named) {
	    ram_decl_at(st,XIDX(ix))->is_mapped = 1;
	    ram_decl_at(st,XIDX(ix))->reg = dst;
	    ap->rmap[dst] = XIDX(ix);
	}
	// A constant loads as an immediate; a #param does NOT. It is a
	// DECL_CONSTANT with `local` set, and its value can change while the
	// program runs, so it has to be LOADED from the slot setup_decl gave it
	// like any variable. Baking it in here would be the same bug as folding
	// it in push_var, one layer down.
	if ((decl(st,XIDX(ix),type) == DECL_CONSTANT) &&
	    !decl(st,XIDX(ix),local)) {
	    value_t val = decl(st,XIDX(ix),cn.init);
	    vtype_t vt = decl(st,XIDX(ix),vt);
	    if (!csp_load_value(st, dst, vt, val))
		return -1;
	    return dst;
	}
	// generate LD instruction for variables and params, track for <- rules
	if (!asm_mem(st,OP_LD,dst,ix))
	    return -1;
	return dst;
    }
    return 0;
}


// generate LD/LI.. load value into a register if not already
NOINLINE int csp_load(csp_rt_t* st, rentry_t* rp)
{
    if (!rp->L && st->cs.ap) { // not loaded and generate code
	int r;

	if (rp->X) {  // load variable
	    if (rp->part != PART_VAL) {  // config part: fresh LDP, never cached
		r = alloc_reg(st);
		if (!asm_mem_part(st, OP_LDP, r, rp->ix, rp->part))
		    return -1;
	    }
	    else if (rp->A) {
		// An array element must NOT go through map_reg. That cache keys
		// on the DECLARATION, and every element of an array shares one
		// -- so `A[j]` would be handed the register still holding
		// `A[i]`. Fresh register, arm the subscript, emit the LD.
		r = alloc_reg(st);
		st->cs.arr_reg = rp->areg;
		st->cs.arr_len = csp_array_len(st, XIDX(rp->ix));
		if (!asm_mem(st, OP_LD, r, rp->ix))
		    return -1;
		free_reg(st, rp->areg);   // the SETOX consumed it
	    }
	    else if ((r = map_reg(st, rp->ix)) < 0)
		return -1;
	}
	else if (rp->I) {
	    r = alloc_reg(st);
	    if (!csp_load_value(st, r, rp->vt, rp->val))
		return -1;
	}
	rp->reg = r;
	rp->L = 1;
    }
    return rp->reg;
}


// Push immediate value (integer, float, or string constant)
NOINLINE static int push_imm(csp_rt_t* st, rentry_t* rstack, int ep,
		    vtype_t vt, value_t val)
{
    rstack[ep] = (rentry_t){ .reg = 0, .val = val, .vt  = vt,
			     .L=0, .I=1, .X=0 };
    return ep+1;
}

// Push string constant. A literal is an IMMEDIATE whose value is a POSITION in
// the string table -- not a reference to a declaration. It used to mint a dummy
// DECL_CONSTANT to hang the position on; now the position is the value itself,
// which is what the rest of the machinery already assumed: csp_load_value does
// `asm_LI(x, val.s)` for V_STRING, i.e. it loads a position as an immediate.
//
// So the entry has to be flagged .I (immediate), not .X (ix is a decl index).
// Left as .X it failed pmatch_const_s's `if (!result.I)` test, so
// `#variable A string = "World"` quietly lost its initialiser -- and anything
// downstream that trusted .X would have read a string position as a decl index.
NOINLINE static int push_str(csp_rt_t* st, rentry_t* rstack, int ep,
		    char* ptr, int len)
{
    value_t v;
    int pos;

    if ((pos = lookup_string(st, ptr, len)) < 0) {
	if ((pos = new_string(st, ptr, len)) < 0)
	    return -1;
    }
    v.s = pos;
    return push_imm(st, rstack, ep, V_STRING, v);
}

// Push variable/declaration reference
NOINLINE static int push_var(csp_rt_t* st, rentry_t* rstack, int ep,
			     xindex_t ix, vtype_t vt)
{
    value_t val;
    int I = 0;

    // A #param is a DECL_CONSTANT with `local` set, and the ONE thing it must not
    // do is fold: its value can change while the program runs, so a rule has to
    // read the slot setup_decl gave it (constants have one anyway -- CT[Idx]
    // needs it). Everything else about it is a constant.
    if ((decl(st,INDEX(ix),type) == DECL_CONSTANT) &&
	!decl(st,INDEX(ix),local)) {
	I = 1;
	val = decl(st,INDEX(ix),cn.init);
    }
    else if ((decl(st,INDEX(ix),type) == DECL_VARIABLE) ||
	     (decl(st,INDEX(ix),type) == DECL_CONSTANT)) {
	add_var(st, ix);
	if (st->cs.ev) {
	    ctx_save_t sv;
	    index_t rx = ctx_enter(st, ix, &sv);
	    I = 1;
	    val = csp_value(st, rx);
	    ctx_leave(st, &sv);
	}
    }
    else {
	val = rstack[ep].val;
    }
    rstack[ep] = (rentry_t) { .ix=ix, .val=val, .L=0, .I=I, .X=1, .vt = vt };
    return ep+1;
}

// Push L-value (assignment target, index only, no load)
NOINLINE static int push_lval(rentry_t* rstack, int ep, xindex_t ix, vtype_t vt)
{
    rstack[ep] = (rentry_t) { .ix=ix,.X=1,.L=0,.I=0,.vt=vt };
    return ep+1;
}

// Push a config-part L-value (<var> '.' <part> '='): defer to the store, don't
// fold to a read the way push_part does -- process_assign turns it into an STP
// (or a direct csp_dio_set_part in immediate mode).
NOINLINE static int push_lval_part(rentry_t* rstack, int ep, xindex_t ix,
				   csp_part_t part, vtype_t vt)
{
    rstack[ep] = (rentry_t) { .ix=ix,.X=1,.L=0,.I=0,.part=part,.vt=vt };
    return ep+1;
}

// Push a config-part read (<var> '.' <part>), e.g. Led.pin, Frame.endian.
// During eval fold it to the current part value; otherwise defer to an LDP.
NOINLINE static int push_part(csp_rt_t* st, rentry_t* rstack, int ep,
			      xindex_t ix, csp_part_t part)
{
    if (st->cs.ev) {
	ctx_save_t sv;
	value_t pv;
	index_t rx = ctx_enter(st, ix, &sv);
	csp_dio_get_part(st, rx, &pv, part, DIN);
	ctx_leave(st, &sv);
	return push_imm(st, rstack, ep, V_INTEGER, pv);
    }
    rstack[ep] = (rentry_t){ .ix=ix, .L=0, .I=0, .X=1, .part=part,
			     .vt=V_INTEGER };
    return ep+1;
}

// Push register result (from operation)
NOINLINE static int push_reg(rentry_t* rstack, int ep, reg_t r, vtype_t vt,
			     value_t val, int imm)
{
    rstack[ep] = (rentry_t){.val=val,.reg=r,.X=0,.I=imm,.L=1,.vt=vt };
    return ep+1;
}

// Convert operand to float (int→float via cvtif)
NOINLINE static bool_t coerce_to_float(csp_rt_t* st, rentry_t* e)
{
    rentry_t ent = *e;

    if (ent.vt == V_FLOAT) return 1;  // already float
    if (ent.vt != V_INTEGER) return 0;  // can only convert int

    // For variables (X=1), load first then convert
    if (ent.X && st->cs.ap) {
	if (csp_load(st, &ent) < 0)
	    return 0;
    }

    if (ent.I && !ent.X) {  // pure immediate, not variable
	ent.val.f = op_CVTIF(ent.val.i);
	ent.I = 1;
	ent.L = 0;
    }
    else if (ent.L && st->cs.ap) {
	reg_t r = alloc_reg(st);
	if (!asm_CVTIF(st, r, ent.reg))
	    return 0;
	free_reg(st, ent.reg);
	ent.reg = r;
	ent.L = 1;
	ent.I = 0;
    }
    ent.vt = V_FLOAT;
    *e = ent;
    return 1;
}

// Convert operand to int (float→int via cvtfi)
NOINLINE static bool_t coerce_to_int(csp_rt_t* st, rentry_t* e)
{
    rentry_t ent = *e;

    if (ent.vt == V_INTEGER) return 1;  // already int
    if (ent.vt != V_FLOAT) return 0;  // can only convert float

    // For variables (X=1), load first then convert
    if (ent.X && st->cs.ap) {
	if (csp_load(st, &ent) < 0)
	    return 0;
    }

    if (ent.I && !ent.X) {  // pure immediate, not variable
	ent.val.i = op_CVTFI(ent.val.f);
	ent.I = 1;
	ent.L = 0;
    }
    else if (ent.L && st->cs.ap) {
	reg_t r = alloc_reg(st);
	if (!asm_CVTFI(st, r, ent.reg))
	    return 0;
	free_reg(st, ent.reg);
	ent.reg = r;
	ent.L = 1;
	ent.I = 0;
    }
    ent.vt = V_INTEGER;
    *e = ent;
    return 1;
}

// Coerce rhs value to the declared type of assignment target ix
NOINLINE static bool_t coerce_assign(csp_rt_t* st, xindex_t ix, rentry_t* e)
{
    vtype_t lt = decl(st,XIDX(ix),vt);

    // A #local BINDS a formula; assigning to it later is the one mistake this
    // construct invites, and "unknown variable" would be a lie -- the name is
    // perfectly well known. Every assignment target in the compiler passes
    // through here, which is why the check lives in this function and not in
    // the three places that build one.
    //
    // The exception is the declaration's OWN rule: csp_parse_local emits
    // `name = <formula>` and marks the target here for the length of that
    // parse (+1, so a zeroed struct means "none" -- decl index 0 is real).
    // A #param carries the same bit but the opposite exception: changing it from
    // OUTSIDE is the point, so an immediate (`> Kp = 7`, st->cs.ev) is allowed
    // and a rule is not. A rule that writes its own configuration is the bug
    // this catches.
    if (decl(st,XIDX(ix),local) &&
	(st->cs.local_def != (index_t)(XIDX(ix) + 1))) {
	int param = (decl(st,XIDX(ix),type) == DECL_CONSTANT);
	if (!(param && st->cs.ev)) {
	    if (csp_set_error(st, param ? ERR_ASSIGN_TO_PARAM
				        : ERR_ASSIGN_TO_LOCAL))
		csp_set_err_arg_int(st, 0, 0);
	    return 0;
	}
    }

    if (e->vt == V_UNSIGNED)  // same representation as int
	e->vt = V_INTEGER;
    if ((lt == V_FLOAT) && (e->vt == V_INTEGER))
	return coerce_to_float(st, e);
    if (((lt == V_INTEGER) || (lt == V_UNSIGNED)) && (e->vt == V_FLOAT))
	return coerce_to_int(st, e);
    return 1;
}

// Process binary assignment operator: generates ST instruction
// Returns new ep on success, -1 on error
NOINLINE static int process_assign(csp_rt_t* st, opcode_t op, rentry_t* rstack, int ep)
{
    rentry_t lhs = rstack[ep-2];
    rentry_t rhs = rstack[ep-1];

#ifdef DEBUG
    if (debug) {
    printf("ASSIGN ");
    print_rentry(st, "lhs", &lhs);
    print_rentry(st, "rhs", &rhs);
    printf("\n");
    }
#endif

    if (lhs.ix == BAD_INDEX) {
	// FIXME: error "left and side is not an lvalue"
	// can we print left hand side? maybe not worth it!
	csp_set_error(st, ERR_SYNTAX);
	return -1;
    }

    // Type conversion to declared target type if needed
    if (!coerce_assign(st, lhs.ix, &rhs))
	return -1;

    // Compiled path: collapse LI+ST into a single STI for a small immediate
    // plain-value store (mirror of EQI). Keeps e.g. State=OFF one instruction.
    if (st->cs.ap && (lhs.part == PART_VAL) && fits_sti(op, &rhs)) {
	// asm_STI reaches asm_seto too, so an array lvalue arms here as well --
	// `P[Idx] = 0` is exactly the shape this fast path exists for, and
	// skipping it would cost an instruction per store in array-heavy code.
	if (lhs.A) {
	    st->cs.arr_reg = lhs.areg;
	    st->cs.arr_len = csp_array_len(st, XIDX(lhs.ix));
	}
	if (!asm_STI(st, 0, lhs.ix, (int8_t)rhs.val.i))
	    return -1;
	if (lhs.A)
	    free_reg(st, lhs.areg);
	rstack[ep-2] = rhs;   // result is the rhs (for chaining A=B=1)
	return ep - 1;
    }

    if (csp_load(st, &rhs) < 0)
	return -1;

    if (!rhs.L && st->cs.ap) {
	// is this an internal error?
	csp_set_error(st, ERR_SYNTAX);  // rhs must have value
	return -1;
    }

    if (!st->cs.ap) {
	// Immediate mode STORES, and a store needs leaf storage. This path is
	// also reached with codegen off while pmatch merely VALIDATES an
	// expression range -- before csp_rt_start has laid the view/heap tables
	// out, csp_view() then indexes a null table. `st->started` is exactly
	// "value ops are now valid" (see csp_rt_start), so an assignment that
	// arrives before it is refused instead of taking the machine down.
	if (!st->started) {
	    csp_set_error(st, ERR_SYNTAX);
	    return -1;
	}
	if (rhs.I) {
	    ctx_save_t sv;
	    index_t lx = ctx_enter(st, lhs.ix, &sv);
	    if (lhs.part != PART_VAL) {  // <var> '.' <part> = imm  (config write)
		csp_dio_set_part(st, lx, rhs.val, lhs.part, DOUT);
		bitset_set(st->dset, st_index(st, lx)); // must commit
		st->es.anyd = CSP_TRUE;
	    }
	    else
		csp_set_value(st, lx, rhs.val);
	    ctx_leave(st, &sv);
	}
    }
    else { // Generate store instruction
	if (lhs.part != PART_VAL) {  // <var> '.' <part> = rhs  -> STP
	    if (!asm_mem_part(st, OP_STP, rhs.reg, lhs.ix, lhs.part))
		return -1;
	}
	else {
	    // `A[i] = rhs`: arm the subscript so asm_seto lays the SETOX down
	    // in front of the store, exactly as the read path does.
	    if (lhs.A) {
		st->cs.arr_reg = lhs.areg;
		st->cs.arr_len = csp_array_len(st, XIDX(lhs.ix));
	    }
	    if (!asm_mem(st, op, rhs.reg, lhs.ix))
		return -1;
	    if (lhs.A)
		free_reg(st, lhs.areg);   // the SETOX consumed it
	}
    }
    // Result is the rhs (for chaining A=B=1)
    rstack[ep-2] = rhs;
    return ep - 1;
}

// Get float version of arithmetic opcode (or same if no float version)
NOINLINE static opcode_t float_op(opcode_t op)
{
    switch(op) {
    case OP_ADD: return OP_FADD;
    case OP_SUB: return OP_FSUB;
    case OP_MUL: return OP_FMUL;
    case OP_DIV: return OP_FDIV;
    case OP_NEG: return OP_FNEG;
    case OP_MOV: return OP_FMOV;
    case OP_LT:  return OP_FLT;
    case OP_LTE: return OP_FLTE;
    case OP_GT:  return OP_FGT;
    case OP_GTE: return OP_FGTE;
    case OP_EQEQ: return OP_FEQEQ;
    case OP_NEQ: return OP_FNEQ;
    default: return op;
    }
}

NOINLINE value_t eval1(csp_rt_t* st, opcode_t op, value_t y)
{
    value_t sx, sy, x;
    int leave;
    csp_instr_t ci = { .a = { .op=op,.x=0,.y=1,.z=2 }};
    
    sx = st->es.reg[0]; sy = st->es.reg[1];
    st->es.reg[1] = y;
    eval_op(st, 0, ci, &leave);
    x = st->es.reg[0];
    st->es.reg[0] = sx; st->es.reg[1] = sy;
    return x;
}


NOINLINE value_t eval2(csp_rt_t* st, opcode_t op, value_t y, value_t z)
{
    value_t sx, sy, sz, x;
    int leave;
    csp_instr_t ci = { .a = { .op=op,.x=0,.y=1,.z=2 }};    
    
    sx = st->es.reg[0]; sy = st->es.reg[1]; sz = st->es.reg[2];
    st->es.reg[1] = y; st->es.reg[2] = z;
    eval_op(st, 0, ci, &leave);
    x = st->es.reg[0];
    st->es.reg[0] = sx; st->es.reg[1] = sy; st->es.reg[2] = sz;    
    return x;
}


NOINLINE static int process_op(csp_rt_t* st, tok_t tok, rentry_t* rstack, int ep)
{
    int dst;
    opcode_t op;
    vtype_t rt;
    int arity = op_table_arity(tok);

    // An operator with nothing under it. Reached whenever the constant folder is
    // handed a position that is not an expression at all -- a pattern probing an
    // alternative does exactly that, and a scan bounded by a stop-set hands over
    // one token when it finds the stop token immediately. `rstack[ep-2]` with
    // ep = 0 then reads BELOW the stack, which is a real read of the frame under
    // it, not a wrong answer.
    if (ep < arity)
	return PARSE_ERROR;

    switch(arity) {
    case 2: {
	rentry_t* a = &rstack[ep-2];
	rentry_t* b = &rstack[ep-1];

	switch(tok) {
	case RIMP:
	    st->cs.rimp = 0;
	    if ((ep = process_assign(st, OP_STIMP, rstack, ep)) < 0)
		return PARSE_ERROR;
	    break;
	case EQ:
	    if ((ep = process_assign(st, OP_ST, rstack, ep)) < 0)
		return PARSE_ERROR;
	    break;
	case COMMA:
	    // comma: side effects already done via ST, just keep right operand
	    if (a->L) free_reg(st, a->reg);
	    *a = *b;
	    ep--;
	    break;
	default: {
	    vtype_t at = a->vt;
	    vtype_t bt = b->vt;

	    // Type coerce: promote to float if either operand is float
	    if (at == V_FLOAT || bt == V_FLOAT) {
		if ((at == V_INTEGER) && !coerce_to_float(st, a))
		    return PARSE_ERROR;
		if ((bt == V_INTEGER) && !coerce_to_float(st, b))
		    return PARSE_ERROR;
		op = float_op(op_table_code(tok));
	    } else {
		op = op_table_code(tok);
	    }
	    rt = csp_opcode_rtype(op);
#ifdef DEBUG
	    if (debug) {
	    printf("op=%s\n", csp_opcode_name(op));
	    print_rentry(st, "L", a);
	    print_rentry(st, "R", b);
	    printf("\n");
	    }
#endif
	    if ((!st->cs.ap || ( !a->X && !b->X ))
		&& a->I && b->I && (csp_opcode_arity(op) == 2)) {
		// constant fold
		value_t result = eval2(st, op, a->val, b->val);
		if (a->L) free_reg(st, a->reg);
		if (b->L && (a->reg != b->reg)) free_reg(st, b->reg);
		a->X = a->L = 0;
		a->I = 1;
		a->val = result;
	    }
	    else {
		if (csp_load(st, a) < 0) return -1;
		if (csp_load(st, b) < 0) return -1;
		if (a->L && b->L) {
		    dst = alloc_reg(st);
		    if (st->cs.ap != NULL) {
			if (!asm_bop(st, op, dst, a->reg, b->reg))
			    return PARSE_ERROR;
			free_reg(st, a->reg);
			if (a->reg != b->reg)
			    free_reg(st, b->reg);
		    }
		    a->reg = dst;
		    a->I = 0;
		    a->vt = rt;
		}
		else if (st->cs.ap)
		    return -1;
		else {
		    a->I = a->L = a->X = 0;
		    a->vt = rt;
		}
	    }
	    ep--;
	}
	}
	break;
    }
    case 1: {
	rentry_t* a = &rstack[ep-1];
	vtype_t at = a->vt;

	// Select float op if operand is float
	if (at == V_FLOAT) {
	    op = float_op(op_table_code(tok));
	} else {
	    op = op_table_code(tok);
	}
	rt = csp_opcode_rtype(op);

#ifdef DEBUG
	if (debug) {
	printf("op=%s\n", csp_opcode_name(op));
	print_rentry(st, "A", a);
	printf("\n");
	}
#endif
	if (!a->X && a->I && (csp_opcode_arity(op) == 1)) { // constant fold
	    value_t result = eval1(st, op, a->val);
	    if (a->L) free_reg(st, a->reg);
	    a->val = result;
	    a->X = a->L = 0;
	    a->I = 1;
	}
	else {
	    if (csp_load(st, a) < 0) return -1;
	    if (a->L) { // generate code
		dst = alloc_reg(st);
		if (st->cs.ap != NULL) {
		    if (!asm_uop(st, op, dst, a->reg))
			return PARSE_ERROR;
		    free_reg(st, a->reg);
		}
		a->reg = dst;
		a->I = 0;
		a->vt = rt;
	    }
	    else if (st->cs.ap)
		return -1;
	    else
		a->I = a->L = a->X = 0;
	}
	a->vt = rt;
	break;
    }
    case 0:
	return PARSE_ERROR;
    }
    return ep;
}

// function flags (FUNC_PURE | FUNC_RONAME)
uint8_t func_flags(const csp_func_t* fn, int i, int rom)
{
    return rd8(&fn[i].flags, rom);
}

#define func_pure(fn,i,rom)   (func_flags((fn),(i),(rom)) & FUNC_PURE)
#define func_roname(fn,i,rom) (func_flags((fn),(i),(rom)) & FUNC_RONAME)

uint8_t fn_type(const csp_func_t* fn, int j, int rom)
{
    uint16_t argtypes = rd16(&fn->argtypes, rom);
    return (argtypes >> 4*j) & 0xf;
}

static uint8_t func_namelen(const csp_func_t* fn,int i, int rom)
{
    return rd8(&fn[i].namelen, rom);
}

uint8_t func_rtype(const csp_func_t* fn, int i, int rom)
{
    return rd8(&fn[i].rtype, rom);
}

// What a call actually returns. A fixed rtype answers for itself; V_NUMBER
// means "the same kind the arguments were", so a single float argument makes
// the whole call float and everything else stays integer. argcode holds the
// ORIGINAL argument types, 4 bits each -- V_NUMBER parameters are not coerced
// on the way in, which is exactly what makes this readable here.
vtype_t call_rtype(uint8_t rtype, uint16_t argcode, int arity)
{
    int j;
    if (rtype != V_NUMBER)
	return (vtype_t) rtype;
    for (j = 0; j < arity; j++) {
	if (((argcode >> 4*j) & 0xf) == V_FLOAT)
	    return V_FLOAT;
    }
    return V_INTEGER;
}

// match function template this code assumes type coerce int->flt
// flt->int. the goal is to match BEST? function to use
// return 0 on match
// return argument number 1...n on mismatch
int csp_match_args(csp_rt_t* st, const csp_func_t* fn, int arity, rentry_t* rarg,
		   int rom)
{
    int j;
    for (j = 0; j < arity; j++) {
	rentry_t arg = rarg[j];
	vtype_t argvt = arg.vt;
	uint8_t ftype = fn_type(fn, j, rom);
	switch(ftype) {
	case V_ANY:
	    break;
	case V_NUMBER:
	    if (argvt == V_INTEGER) break;
	    if (argvt == V_FLOAT) break;
	    goto mismatch;
	case V_INTEGER:
	    if (argvt == V_INTEGER) break;
	    if (argvt == V_FLOAT) break;    // coerce!
	    goto mismatch;
	case V_FLOAT:
	    if (argvt == V_FLOAT) break;
	    if (argvt == V_INTEGER) break;  // coerce!
	    goto mismatch;
	case V_STRING:
	    if (argvt == V_STRING) break;
	    goto mismatch;
	case V_INDEX:
	    if (arg.X) break;
	    goto mismatch;
	case V_TIMER:
	    if (arg.X && (decl(st,INDEX(arg.ix),type) == DECL_TIMER)) break;
	    goto mismatch;
	case V_DIGITAL:
	    if (arg.X && (decl(st,INDEX(arg.ix),type) == DECL_DIGITAL)) break;
	    goto mismatch;
	case V_ANALOG:
	    if (arg.X && (decl(st,INDEX(arg.ix),type) == DECL_ANALOG)) break;
	    goto mismatch;
	case V_FIELD:
	    if (arg.X && (decl(st,INDEX(arg.ix),type) == DECL_FIELD)) break;
	    goto mismatch;
	default:
	    goto mismatch;
	}
    }
    return 0;
mismatch:
    return j+1;
}

static const char* func_name(const csp_func_t* fn, int i, int rom)
{
    return (const char*) rdvp(&fn[i].name, rom);
}


static int csp_match_fn(csp_rt_t* st,
			const csp_func_t* fn, int num, int rom,
			const tstr_t* name,
			uint8_t arity, rentry_t* rarg)
{
    int i;
    int a, f = -1;
    for (i = 0; i < num; i++) {
	if ((func_arity(fn,i,rom) == arity) &&
	    (func_namelen(fn,i,rom) == name->len)) {
	    const char* fnm = func_name(fn, i, rom);
	    int eq = func_roname(fn,i,rom)
		? (ro_memcmp(name->ptr, fnm, name->len) == 0)   // name in ROM
		: (memcmp(name->ptr, fnm, name->len) == 0);     // name in RAM
	    if (eq) {
		int j;
		if ((j=csp_match_args(st, &fn[i], arity, rarg, rom)) == 0) // ok
		    return i;
		f = i;  // last name match
		a = j;  // and argument poistion that failed
	    }
	}
    }
    if (f >= 0) {
	if (csp_set_error(st, ERR_FUNCTION_ARGUMENT_TYPE_MISMATCH)) {
	    csp_set_err_arg_tstr(st, 0, name);
	    csp_set_err_arg_int(st, 1, arity);
	    csp_set_err_arg_int(st, 2, a);
	}
    }
    return -1;
}

const csp_func_t* csp_match_func(csp_rt_t* st,
				 const tstr_t* name,
				 uint8_t arity, rentry_t* rarg,
				 int* is_user, int* func_idx)
{
    int idx;

    if (st->ufuncs) {
	if ((idx = csp_match_fn(st, st->ufuncs, st->num_ufuncs, st->ufuncs_rom,
				name, arity, rarg)) >= 0) {
	    *is_user = 1;
	    *func_idx = idx;
	    return &st->ufuncs[idx];
	}
    }
    if ((idx = csp_match_fn(st, csp_builtin_funcs, csp_num_builtin_funcs, BUILTIN_ROM,
			    name, arity, rarg)) >= 0) {
	*is_user = 0;
	*func_idx = idx;
	return &csp_builtin_funcs[idx];
    }
    if (csp_set_error(st, ERR_FUNCTION_DOES_NOT_EXIST)) {
	csp_set_err_arg_tstr(st, 0, name);
	csp_set_err_arg_int(st, 1, arity);
    }
    return NULL;
}

//       rstack
//  0    arg0   ep-(4-0)
//  1    arg1   ep-(4-1)
//  2    arg2   ep-(4-2)
//  3    arg3   ep-(4-3)
//  ep
//
NOINLINE static int process_fcall(csp_rt_t* st, const token_t* word,
				  uint8_t arity, rentry_t* rstack, int ep)
{
    int dst, n, j;
    const csp_func_t* func = NULL;
    uint16_t argcode = 0;
    uint8_t argimm = 0;
    unsigned call_obj = XOBJ_GLOBAL;    // object an index argument named, if any
    // int func_res;
    int is_user;
    int numflt = 0;                     // any V_NUMBER argument is float
    int func_idx;
    int from;                           // func table in ROM?
    rentry_t* rarg = &rstack[ep-arity]; // first arg
    value_t dval = {.u = 0};   // result value when folded; 0 keeps it defined
    int imm = 0;

    if ((func = csp_match_func(st, &word->v.str, arity,
			       rarg, &is_user, &func_idx)) == NULL)
	return -1;
    from = is_user ? st->ufuncs_rom : BUILTIN_ROM;
    // FIXME: handle, changed(x), timeout(t) with ops
    n = arity;
    // A V_NUMBER parameter is NOT coerced -- the argument keeps its own
    // representation and avt tells the callee which it got. With more than one
    // such parameter they must still agree, or fn_min would compare a plain
    // integer against a fixpoint word. So settle the common type first: one
    // float argument makes every V_NUMBER position float.
    for (j = 0; j < n; j++) {
	if ((fn_type(func, j, from) == V_NUMBER) && (rarg[j].vt == V_FLOAT)) {
	    numflt = 1;
	    break;
	}
    }
    for (j = 0; j < n; j++) {
	rentry_t arg = rarg[j];
	vtype_t argvt = arg.vt;
	vtype_t argtype = fn_type(func, j, from); // read RO data!

	if ((argtype == V_NUMBER) && numflt)
	    argvt = V_FLOAT;              // what the callee will actually see

	argcode |= (argvt << 4*j);
	argimm  |= (arg.I << j);

	// check arguments & coerce
	switch(argtype) {
	case V_ANY:
	    break;  // OK
	case V_NUMBER:
	    if (!((arg.vt == V_INTEGER) || (arg.vt == V_FLOAT)))
		goto type_mismatch;
	    if (numflt && (arg.vt == V_INTEGER) && !coerce_to_float(st, &arg))
		goto type_mismatch;
	    break;
	case V_INTEGER:
	    if (!coerce_to_int(st, &arg))
		goto type_mismatch;
	    break;
	case V_FLOAT:
	    if (!coerce_to_float(st, &arg))
		goto type_mismatch;
	    break;
	case V_STRING:
	    if (argvt != V_STRING)
		goto type_mismatch;
	    break;
	case V_TIMER:
	case V_DIGITAL:
	case V_ANALOG:
	case V_FIELD:
	case V_INDEX:
	    // check object type !!!
	    if (!arg.X) { // must be a "variable"
		goto type_mismatch;
	    }
	    arg.X = 0;
	    arg.I = 1;
	    // An index passed BY VALUE -- timeout(T), changed(X) -- has to be a
	    // narrow ENCODED index: the callee resolves it with st_index. So it
	    // is narrowed here, and if it names an object the OP_SETO goes in
	    // front of the CALL instead of in front of a memory instruction. The
	    // callee then resolves CURRENT-relative inside that object, and
	    // eval_op's tail puts the context back afterwards.
	    {
		unsigned m = XOBJ(arg.ix);
		if ((m != XOBJ_GLOBAL) && (m != XOBJ_CURRENT)) {
		    // One OP_SETO can only point at one object, and it is
		    // consumed by the single instruction after it.
		    if ((call_obj != XOBJ_GLOBAL) && (call_obj != m)) {
			csp_set_error(st, ERR_SYNTAX);
			return -1;
		    }
		    call_obj = m;
		}
		arg.val.u = MAKE_INDEX(m ? CURRENT : GLOBAL, XIDX(arg.ix));
	    }
	    break;
	default:
	    if (argtype != argvt)
		goto type_mismatch;
	    break;
	}
	// Hand the COERCED value back to the stack entry: the constant fold
	// below reads rarg[j].val, so without this an int promoted to float
	// would be folded as a raw integer bit pattern.
	rarg[j].val = arg.val;
	if (csp_load(st, &arg) < 0) {
	    csp_set_error(st, ERR_INTERNAL_ERROR);
	    return -1;
	}
	if (!asm_ARG(st, arg.reg, j)) {
	    csp_set_error(st, ERR_INTERNAL_ERROR);
	    return -1;
	}
	if (arg.L) free_reg(st, arg.reg);
    }
    // check if we can evaluate a pure function
    // note that we may have generated arguments anyway, may be
    // optimise to remove extra instructions in the future
    // we may have to do a "dryrun" to check if function is pure
    // or we have special functions
    imm = (argimm == ((1 << arity)-1));
    // Fold when all args are immediate AND either the function is pure, or we
    // are in eval mode (st->cs.ev, i.e. an immediate `> expr` at the prompt) where
    // even an impure call like latch()/print() must run now for its side effect
    // -- otherwise the emitted OP_CALL is never executed and the call no-ops.
    if (imm && (func_pure(func,0,from) || st->cs.ev)) {
	value_t arg[MAX_ARGS];
	csp_func_fn fn = NULL;

	if (is_user)
	    fn = func_fn(st->ufuncs, func_idx, st->ufuncs_rom);
	else
	    fn = func_fn(csp_builtin_funcs, func_idx, BUILTIN_ROM);
	for (j = 0; j < arity; j++)
	    arg[j] = rarg[j].val;
	dval = fn(st, argcode, arg, arity);
    }

    // pop rstack
    if (n > 0) {
	ep -= n;
    }
    dst = alloc_reg(st);
    // An index argument named an object: bind it for the CALL itself.
    if (call_obj != XOBJ_GLOBAL) {
	csp_instr_t* sp = alloc_instr_ptr(st, NULL, OP_SETO);
	if (sp == NULL)
	    return -1;
	sp->o.obj = call_obj;
    }
    if (!asm_CALL(st, dst, func_idx, is_user, argcode))
	return -1;
    return push_reg(rstack, ep, dst,
		    call_rtype(func_rtype(func, 0, from), argcode, arity),
		    dval, imm);

type_mismatch:
    if (csp_set_error(st, ERR_FUNCTION_ARGUMENT_TYPE_MISMATCH)) {
	csp_set_err_arg_tstr(st, 0, &word->v.str);
	csp_set_err_arg_int(st, 1, arity);
	csp_set_err_arg_int(st, 2, j);
    }
    return -1;
}

void print_stack_used()
{
    // stack debug
    csp_print_lit("StackUsed=");
    csp_print_int(stack_used());
    csp_println();
}

// num_toks is number of tokens on input, consumed on output
// result receives the expression result (reg, immediate flag, value, type)
// returns: 1=ok, 0=error
NOINLINE index_t make_buf_view(csp_rt_t* st, xindex_t parent,
			       ivalue_t b0, ivalue_t b1);

NOINLINE int csp_parse_expr(csp_rt_t* st, const token_t* tv, size_t* num_toks,
		   rentry_t* result)
{
    tok_t tok;
    tokval_t tval;
    tok_t ptok = NONE;   // previous operator/token
    int pp = 0;         // operator stack pointer
    int ep = 0;         // expression stack pointer
    uint32_t ostack[MAX_PARSE_STACK_DEPTH];  // stack of operators
    rentry_t rstack[MAX_PARSE_STACK_DEPTH];  // stack of {reg, index}
    xindex_t ix;   // may name an object until asm_* narrows it
    int i = 0;
    size_t n = *num_toks;
    int in_func = 0;

    csp_stack_mark();   // deepest known point (the expression parser + pmatch
			// recursion); this is where margin bottoms out

next:
    if ((i >= n) || (tv[i].t==NEWLINE) || (tv[i].t==NONE))  // end-of-list
	goto out;
    tok  = tv[i].t;
    tval = tv[i].v;
    i++;
    switch(tok) {
    case QUEST: i--; goto out;
    case PLUS:
	if ((ptok == NONE) || (ptok == RP) ||
	    ((ptok != INT) && (ptok != WORD) && (ptok != FLT)))
	    tok = PLUS1;
	goto operator;
    case MINUS:
	if ((ptok == NONE) || (ptok == RP) ||
	    ((ptok != INT) && (ptok != WORD) && (ptok != FLT)))
	    tok = MINUS1;
	goto operator;
    case LP:
	ostack[pp++] = LP; ptok = LP; break;
    case RB: {
	// Close an array subscript: reduce the index expression, take the
	// register it landed in, and push the ELEMENT as the primary the whole
	// `A[...]` was. The register stays allocated -- it is read by the SETOX
	// that asm_seto lays down when the access is finally emitted, which may
	// be a long way from here (`A[i] = B[j] + 1` has two live at once).
	uint32_t marker;
	xindex_t aix;
	int ep0;
	uint16_t alen;
	vtype_t avt;
	reg_t r;

	while (pp && !IS_MARKER(ostack[pp-1]) && (ostack[pp-1] != LP)) {
	    if ((ep = process_op(st, ostack[pp-1], rstack, ep)) < 0)
		return 0;
	    pp--;
	}
	if (!pp || !IS_ARR_MARKER(ostack[pp-1])) {
	    csp_set_error(st, ERR_SYNTAX);      // `]` with no `A[` open
	    return 0;
	}
	marker = ostack[--pp];
	ep0 = ARR_MARKER_EP(marker);
	aix = MAKE_XINDEX(ARR_MARKER_CUR(marker) ? XOBJ_CURRENT : XOBJ_GLOBAL,
			  ARR_MARKER_IX(marker));
	// Exactly one value between the brackets: `A[]` leaves none, `A[i,j]`
	// leaves two. Both are the user's mistake, not something to guess at.
	if (ep != ep0 + 1) {
	    csp_set_error(st, ERR_SYNTAX);
	    return 0;
	}
	alen = csp_array_len(st, XIDX(aix));
	avt  = decl(st,XIDX(aix),vt);

	// A CONSTANT subscript needs no instruction at all: fold it into the
	// element's own declaration index. `A[2]` then costs exactly what `A`
	// costs, its bounds check happens HERE instead of every cycle, and
	// immediate mode (`> A[1] = 5` at the prompt, where there is no
	// instruction stream to put a SETOX in) works by the same path.
	if (rstack[ep-1].I && !rstack[ep-1].L) {
	    ivalue_t k = rstack[ep-1].val.i;
	    if ((k < 0) || (k >= (ivalue_t)alen)) {
		csp_set_error(st, ERR_INDEX_RANGE);
		return 0;
	    }
	    ep--;
	    aix = MAKE_XINDEX(XOBJ(aix), XIDX(aix) + (index_t)k);
	    if ((i < n) && ((tv[i].t == EQ) || (tv[i].t == RIMP)))
		ep = push_lval(rstack, ep, aix, avt);
	    else if ((ep = push_var(st, rstack, ep, aix, avt)) < 0)
		return 0;
	    ptok = WORD;
	    goto after_primary;
	}
	// A runtime subscript becomes a SETOX in front of the access. With
	// codegen OFF nothing is emitted here either: pmatch validates an
	// expression range with cs.ap NULL before asm_rule compiles it for real,
	// so this runs twice and only the second pass lays instructions down.
	// Erroring on the first pass rejected every runtime subscript there is.
	if (st->cs.ap != NULL) {
	    if (csp_load(st, &rstack[ep-1]) < 0)
		return 0;
	    r = rstack[ep-1].reg;
	}
	else
	    r = 0;
	ep--;                                   // subscript leaves the stack
	// The entry is built here rather than through push_var, which FOLDS a
	// DECL_CONSTANT to its init value. That is right for a scalar -- `A = B`
	// and `A = 5` are the same code -- and wrong for `CT[Idx]`, where the
	// element is not known until the program runs: folding would bake
	// element 0 into every reference. A deferred access is emitted instead,
	// and the constant's own storage (setup_decl allocates it) is read.
	rstack[ep] = (rentry_t) { .ix = aix, .vt = avt,
				  .L = 0, .I = 0, .X = 1,
				  .A = 1, .areg = r };
	ep++;
	ptok = WORD;
	goto after_primary;
    }
    case RP:
	if (pp == 0)
	    return 0;
	// Process operators until we hit LP or a function marker
	// Check if we're inside a function call

	for (int k = pp-1; k >= 0; k--) {
	    // Stop at the nearest marker of EITHER kind: an array subscript
	    // between here and the call means the comma is not this call's.
	    if (IS_MARKER(ostack[k])) {
		in_func = IS_FUNC_MARKER(ostack[k]);
		break;
	    }
	    if (ostack[k] == LP) break;
	}
	while(pp && ((tok = ostack[pp-1]) != LP) && !IS_MARKER(tok)) {
	    // COMMA inside function call: just pop it, don't combine args
	    if (tok == COMMA && in_func) {
		pp--;
		continue;
	    }
	    if ((ep = process_op(st, tok, rstack, ep)) < 0)
		return 0;
	    pp--;
	}
	if (pp && IS_FUNC_MARKER(ostack[pp-1])) {
	    uint32_t marker = ostack[--pp];
	    int j = FUNC_MARKER_TIX(marker);
	    int ep0 = FUNC_MARKER_EP(marker);
	    uint8_t arity = ep - ep0;

	    if ((ep = process_fcall(st, &tv[j], arity, rstack, ep)) < 0) {
		csp_set_error(st, ERR_SYNTAX);
		return 0;
	    }
	}
	else if (pp && (ostack[pp-1] == LP)) {
	    pp--;  // pop the LP for regular parentheses
	}
	else {
	    return 0;  // mismatched )
	}
	tok = RP;
	ptok = INT;
	break;
    case INT:
	if ((ep = push_imm(st, rstack, ep, V_INTEGER, tval.val)) < 0)
	    return 0;
	ptok = INT;
	goto after_primary;
    case FLT:
	if ((ep = push_imm(st, rstack, ep, V_FLOAT, tval.val)) < 0)
	    return 0;
	ptok = FLT;
	goto after_primary;
    case STR:
	if ((ep = push_str(st, rstack, ep, tval.str.ptr, tval.str.len)) < 0)
	    return 0;
	ptok = STR;
	goto after_primary;
    case T_IN:  case T_OUT:  case T_INOUT: // dir keyword as int  (Led.dir=out)
    case T_NATIVE: case T_LITTLE: case T_BIG: {  // endian keyword as int
	value_t kv;
	kv.i = (tok==T_IN)    ? DIR_IN  : (tok==T_OUT)    ? DIR_OUT   :
	       (tok==T_INOUT) ? DIR_INOUT :
	       (tok==T_NATIVE) ? E_NATIVE : (tok==T_LITTLE) ? E_LITTLE : E_BIG;
	if ((ep = push_imm(st, rstack, ep, V_INTEGER, kv)) < 0)
	    return 0;
	ptok = INT;
	goto after_primary;
    }
    case WORD: {
	if (tv[i].t == LP) {
	    // It's a function call - push marker to ostack and skip LP
	    ostack[pp] = MAKE_FUNC_MARKER(i-1, ep);
	    pp++;
	    i++;  // skip the LP token
	    ptok = LP;
	}
	else {
	    vtype_t vt;
	    // Not a function - regular variable/decl/state lookup.
	    //
	    // A state name resolves through csp_lookup_decl too now: states are
	    // DECL_STATES declarations and share the one namespace. So the type
	    // decides what it IS, rather than a fallback deciding it after the
	    // first lookup came up empty -- which is what makes `x = red` and
	    // `State = red` mean the same thing about `red`.
	    //
	    // Falling through here as if a block were a variable is not merely
	    // wrong, it CORRUPTS: map_reg caches a register by writing is_mapped
	    // and reg through DECL_COMMON, and those bits are the low six of
	    // name3. `State = c` used to overwrite the third state's name.
	    ix = csp_lookup_decl(st,&tval.str);
	    if ((ix == BAD_INDEX) ||
		(decl(st,XIDX(ix),type) == DECL_STATES)) {
		int s = lookup_state(st, &tval.str);
		if (s >= 0) {
		    value_t sv;
		    sv.i = s;   // lookup_state returns the number
		    if ((ep = push_imm(st, rstack, ep, V_INTEGER, sv)) < 0)
			return 0;
		    ptok = INT;
		    goto after_primary;
		}
		if (csp_set_error(st, ERR_VARIABLE_NOT_DECLARED)) {
		    csp_set_err_arg_tstr(st, 0, &tval.str);
		}
		return 0;
	    }
	    // Handle obj.field access
	    if ((decl(st,INDEX(ix),type) == DECL_OBJECT) &&
		(tv[i].t == DOT) && (tv[i+1].t == WORD)) {
		index_t mx = decl(st,INDEX(ix),mq.mx);  // module def
		ivalue_t dn = decl(st,INDEX(mx),md.n);  // number of elements
		index_t jx;
		tval = tv[i+1].v;
		if ((jx = lookup_decl_in(st, &tval.str,
					 INDEX(mx)+1,INDEX(mx)+1+dn)) == BAD_INDEX) {
		    if (csp_set_error(st, ERR_FIELD_NOT_FOUND)) {
			csp_set_err_arg_tstr(st, 0, &tval.str);
		    }
		    return 0;
		}
		// A NAMED object. asm_mem_part/asm_memi turn this into an OP_SETO
		// in front of the access; until then the object rides in the high
		// half of the xindex.
		ix = MAKE_XINDEX(decl(st,INDEX(ix),mq.m), XIDX(jx));
		i += 2;
	    }
	    // Apply module context
	    if ((XOBJ(ix) == XOBJ_GLOBAL) && is_module_local(st, ix))
		ix = MAKE_XINDEX(XOBJ_CURRENT, XIDX(ix));

	    // Buf[pos] / Buf[pos0..pos1] -- byte access on a buffer
	    if ((i < n) && (tv[i].t == LB) &&
		(decl(st,INDEX(ix),type) == DECL_BUFFER)) {
		ivalue_t p0, p1;
		i++;                                   // '['
		if (tv[i].t != INT) { csp_set_error(st, ERR_SYNTAX); return 0; }
		p0 = p1 = tv[i].v.val.i; i++;
		if (tv[i].t == DOTDOT) {
		    i++;
		    if (tv[i].t != INT) {csp_set_error(st,ERR_SYNTAX); return 0;}
		    p1 = tv[i].v.val.i; i++;
		}
		if (tv[i].t != RB) { csp_set_error(st, ERR_SYNTAX); return 0; }
		i++;                                   // ']'
		if ((ix = make_buf_view(st, ix, p0*8, (p1+1)*8-1)) == BAD_INDEX)
		    return 0;
	    }

	    // <var> '.' <part>  -- config part read (obj.field handled above)
	    if ((i+1 < n) && (tv[i].t == DOT) && (tv[i+1].t == WORD) &&
		(decl(st,INDEX(ix),type) != DECL_OBJECT)) {
		csp_part_t pt = part_from_tstr(&tv[i+1].v.str);
		if (pt != PART_LAST) {
		    i += 2;
		    // '=' / '<-' after the part -> assignment target (STP), else
		    // a plain part read (LDP / eval-fold).
		    if ((i < n) && ((tv[i].t == EQ) || (tv[i].t == RIMP))) {
			ep = push_lval_part(rstack, ep, ix, pt,
					    decl(st,INDEX(ix),vt));
		    }
		    else if ((ep = push_part(st, rstack, ep, ix, pt)) < 0)
			return 0;
		    ptok = WORD;
		    goto after_primary;
		}
	    }

	    // A[expr] -- array element with a runtime index. The subscript is an
	    // ordinary expression, so it is parsed by THIS loop: push a marker
	    // that acts like LP (nothing reduces past it) and let RB close it.
	    // No recursion into csp_parse_expr -- csp_stack_mark sits in this
	    // function because it is where the margin bottoms out on AVR, and a
	    // nested call would double the deepest path.
	    if ((i < n) && (tv[i].t == LB) &&
		is_subscriptable(decl(st,INDEX(ix),type))) {
		unsigned mo = XOBJ(ix);
		// `safe.A[i]` would need OP_SETO and OP_SETOX at the same time,
		// and both are one-shots consumed by the same access. Refusing
		// beats emitting a pair where one silently wins.
		if (((mo != XOBJ_GLOBAL) && (mo != XOBJ_CURRENT)) ||
		    (pp >= MAX_PARSE_STACK_DEPTH)) {
		    csp_set_error(st, ERR_SYNTAX);
		    return 0;
		}
		ostack[pp++] = MAKE_ARR_MARKER(mo == XOBJ_CURRENT,
					       XIDX(ix), ep);
		i++;              // skip '['
		ptok = LP;        // a subscript opens like a paren
		goto next;
	    }

	    // Check if this is an l-value (assignment target)
	    vt = decl(st,INDEX(ix),vt);
	    if ((i < n) && ((tv[i].t == EQ)||(tv[i].t == RIMP))) {
		// L-value: push index only, no load
		ep = push_lval(rstack, ep, ix, vt);
	    }
	    else {
		if ((ep = push_var(st, rstack, ep, ix, vt)) < 0)
		    return 0;
	    }
	    ptok = WORD;
	    goto after_primary;
	}
	break;
    }
    default:
	if (op_table_arity(tok) > 0)
	    goto operator;
	i--;      // return failed token
	goto out; // let tok terminate exprssion
	// return 0;
    }
    goto next;

after_primary:
    // After parsing a primary (INT, FLT, STR, WORD), check if next token
    // can continue the expression. If next is another primary, terminate.
    if (i < n) {
	switch (tv[i].t) {
	case INT: case FLT: case STR: case WORD:
	    goto out;  // next primary terminates expression
	default:
	    break;
	}
    }
    goto next;
operator:
    {
	int p1;
	if ((p1 = op_table_prec(tok)) == -1)
	    return 0;
	if (pp == 0) {
	    if (tok == RIMP) { st->cs.rimp = 1; }
	    ostack[pp++] = tok;
	}
	else {
	    tok_t tok2 = ostack[pp-1];
	    int p2;
	    // Either marker acts like LP - don't process operators past it
	    if (IS_MARKER(tok2) || tok2 == LP) {
		if (tok == RIMP) { st->cs.rimp = 1;  }
		ostack[pp++] = tok;
		ptok = tok;
		goto next;
	    }
	    p2 = op_table_prec(tok2);

	    while ( ((p2 > p1) && (tok2 != LP)) ||
		    ((p2 == p1) && (op_table_assoc(tok2) < 0))) {
		if ((ep = process_op(st, tok2, rstack, ep)) < 0)
		    return 0;
		pp--;
		if (pp == 0) break;
		tok2 = ostack[pp-1];
		if (IS_MARKER(tok2) || (tok2 == LP)) break;
		p2 = op_table_prec(tok2);
	    }
	    if (tok == RIMP) { st->cs.rimp = 1; }
	    ostack[pp++] = tok;
	}
	ptok = tok;
    }
    goto next;
out: // expression is terminated with non-expression char
    while(pp > 0) {
	tok = ostack[--pp];
	if (tok == LP)
	    return 0;
	if ((ep = process_op(st, tok, rstack, ep)) < 0)
	    return 0;
    }
    if (pp < 0)
	return 0;
    if (ep == 1) {
	// printf("PARSE_EXPR tokens=%d\n", i);
	*num_toks = i;
	if (result)
	    *result = rstack[0];
	return 1;
    }
    return 0;
}

// parse expr while turn of codegen is the same as partial eval
NOINLINE int csp_parse_const_expr(csp_rt_t* st,
				  const token_t* tv, size_t* num_toks,
				  rentry_t* result)
{
    reg_allocator_t* saved_ap = st->cs.ap;
    int r;
    st->cs.ap = NULL;  // no codegen
    r = csp_parse_expr(st, tv, num_toks, result);
    st->cs.ap = saved_ap;
    return r;
}




// ':' <size> for anything that is a scalar (#variable, #constant, #analog,
// #field). The width has to fit DECL_COMMON.res, which is 5 bits holding
// bits-1: `#variable X:40` used to wrap to 8 and give a silently wrong field.
// (#buffer does NOT come here -- its size is bytes and lives in bf.nbytes.)
NOINLINE static int check_res(csp_rt_t* st, ivalue_t res)
{
    if ((res < 1) || (res > MAX_RES_BITS)) {
	csp_set_error(st, ERR_NUMBER_RANGE);
	return -1;
    }
    return 0;
}


// '#' 'module' <name>
NOINLINE int csp_parse_module(csp_rt_t* st, token_t* tv, int ti, size_t n)
{
    module_param_t d;
    index_t ix;
    int jx;
    int i;

    if (pmatch(st, tv, ti, n, pat_module, &d, sizeof(d)) < 0) {
	csp_set_error(st, ERR_SYNTAX);
	return -1;
    }
    // Mark before anything is emitted: this is where an aborted module rewinds to.
    csp_pstate_save(st, &st->cs.mod_mark);
    if ((ix = csp_new_udecl(st,&d.name,DECL_MODULE)) == BAD_INDEX)
	return -1;
    {
	// create a local state variable (if states are supported)
	// maybe only if #states are defined in module context?
	RO_TSTR(State, ros_State);
	index_t ix;
	st->cs.save_sx = st->cs.sx;
	ix = csp_new_decl(st,&State,DECL_VARIABLE,1);
	st->cs.sx = MAKE_XINDEX(XOBJ_CURRENT, XIDX(ix));
    }

    st->cs.mdef = ix;  // current module being defined
    if (!asm_ENTER(st, &jx, 0, ix))
	return -1;
    st->cs.ent = jx;   // entry point of module being defined
    i = INDEX(ix);
    ram_decl_at(st,i)->md.n = 0;
    ram_decl_at(st,i)->md.ent = st->cs.ent;
    return 0;
}


// '#' 'end' [....]
NOINLINE int csp_parse_end(csp_rt_t* st, token_t* tv, int ti, size_t n)
{
    end_param_t d;
    index_t mx, ex;
    int lx;
    const tstr_t empty = { .ptr = NULL, .len = 0};
    
    if (pmatch(st, tv, ti, n, pat_end, &d, sizeof(d)) < 0) {
	csp_set_error(st, ERR_SYNTAX);
	return -1;
    }

    if (st->cs.sdef >= 0) {
	// close the #in block: patch OP_INSTATE.nxt to jump past everything
	// emitted since the gate, so a State mismatch skips the whole block.
	close_in_block(st);
	return 0;
    }
    if ((mx = st->cs.mdef) == BAD_INDEX) {
	csp_set_error(st, ERR_END_MISMATCH);
	return -1;  // no module
    }
    if ((ex = csp_new_decl(st,&empty,DECL_END,0)) == BAD_INDEX)
	return -1;
    ram_decl_at(st, INDEX(mx))->md.n = (INDEX(ex) - INDEX(mx)) - 1;
    if (!asm_LEAVE(st, &lx, 0, 0))
	return -1;
    // ent MUST be OP_ENTER!
    ram_instr_at(st, st->cs.ent)->e.num = (lx - st->cs.ent - 1);
    ram_instr_at(st, lx)->v.num = instr(st, st->cs.ent, e.num);
    ram_instr_at(st, lx)->v.mx  = instr(st, st->cs.ent, e.mx);
    // stack?
    st->cs.mdef = BAD_INDEX;
    st->cs.ent = 0;
    st->cs.sx   = st->cs.save_sx;
    return 0;
}

// `<name>[N]` in a DECLARATION: read the length and splice the `[N]` out of the
// token vector, so the rest of that declaration's grammar (':res', options,
// '= init', 'bind', a port:pin) is matched exactly as it was. An array differs
// only in how MANY declarations it makes, never in what they say -- which is
// what lets one helper serve #variable, #constant and (next) the device types
// instead of four copies of the same splice.
//
// alen is left alone when there is no subscript, so the caller initialises it
// to 1 and a plain declaration flows through untouched.
NOINLINE static int array_splice(csp_rt_t* st, token_t* tv, int ti,
				 size_t* np, ivalue_t* alen)
{
    size_t n = *np;

    if (!((ti+3 < (int)n) && (tv[ti+1].t == LB) && (tv[ti+2].t == INT) &&
	  (tv[ti+3].t == RB)))
	return 0;
    *alen = tv[ti+2].v.val.i;
    // The upper bound is the SETOX len field, not a taste limit -- an array
    // longer than it can express would be emitted with a wrapped bounds check,
    // i.e. an unchecked one.
    if ((*alen < 1) || (*alen > 0xffff) ||
	(st->ps.nd + *alen > MAX_DECLS)) {
	csp_set_error(st, ERR_NUMBER_RANGE);
	return -1;
    }
    memmove(&tv[ti+1], &tv[ti+4], (n - (ti+4))*sizeof(token_t));
    *np = n - 3;
    return 0;
}

// The elements after the head: copies of the FULLY BUILT head, so they inherit
// everything its own grammar decided (width, direction, init, a bind). Only the
// head keeps the name -- a continuation with name 0 is invisible to
// lookup_decl_in (`d.name > 0`), so `A` resolves to the head and nothing else,
// with no special case in the lookup every reference goes through.
//
// They land CONTIGUOUSLY above the head, so a caller that needs to vary one
// field per element (a constant's value, next a device's pin) writes it with
// ram_decl_at(st, head + k) instead of this taking a callback.
NOINLINE static int array_replicate(csp_rt_t* st, int head, decl_t type,
				    ivalue_t alen)
{
    int k;

    for (k = 1; k < (int)alen; k++) {
	index_t jx;
	if ((jx = csp_new_decl(st, NULL, type, 0)) == BAD_INDEX)
	    return -1;
	*ram_decl_at(st, INDEX(jx)) = *ram_decl_at(st, head);
	ram_decl_at(st, INDEX(jx))->name = 0;
	ram_decl_at(st, INDEX(jx))->cont = 1;
    }
    return 0;
}

// One element's port and pin. A device array shares ONE declaration's worth of
// config with its copies, and the port:pin pair is what has to differ -- which
// is possible at all because both live in the per-element STORAGE, seeded from
// the declaration by setup_digital/setup_analog.
NOINLINE static int set_elem_pin(csp_rt_t* st, int di, decl_t type,
				 ivalue_t port, ivalue_t p)
{
    if ((p < 0) || (p >= (1 << PIN_BITS)) ||
	(port < 0) || (port >= (1 << PORT_BITS))) {
	csp_set_error(st, ERR_NUMBER_RANGE);
	return -1;
    }
    if (type == DECL_DIGITAL) {
	ram_decl_at(st, di)->di.port = (unsigned)port;
	ram_decl_at(st, di)->di.pin  = (unsigned)p;
    }
    else {
	ram_decl_at(st, di)->an.port = (unsigned)port;
	ram_decl_at(st, di)->an.pin  = (unsigned)p;
    }
    return 0;
}

// The pins for elements 1..alen-1 of a device array, from whatever follows the
// `port:pin` the declaration already matched:
//
//   9:0..9              a range   -- element k gets pin 0+k
//   0:1,4,7             a list    -- element k gets the k-th pin
//   0:1..5,7,9          both, in any order
//   1:1..3,2:1,3,5      SEVERAL PORTS -- a port names the ones after it
//   9:,7..9             a port on its own, pins after the comma
//
// Every item is matched by pat_pin_item; this only walks the items and turns
// them into elements, so the grammar lives in one place and the port is just
// another thing an item may carry. `port`/`last` are where an unqualified pin
// and a continuing range pick up from.
//
// A count that does not match the declared length is an ERROR -- ten elements
// over four pins is a typo, and padding or truncating it silently would leave
// elements pointing at pin 0, which is a real pin.
NOINLINE static int array_pins(csp_rt_t* st, const token_t* tv, int j, size_t n,
			       int head, decl_t type, ivalue_t alen,
			       ivalue_t port0, ivalue_t pin0)
{
    ivalue_t port = port0;
    ivalue_t last = pin0;
    int k = 1;

    while (j < (int)n) {
	pin_item_t it;
	ivalue_t lo;
	int r;

	it.num = it.lo = it.hi = -1;
	it.colon = it.sep = 0;
	if ((r = pmatch(st, tv, j, n, pat_pin_item, &it, sizeof(it))) <= j)
	    break;                      // not part of the pin spec
	j = r;
	// `num` is the port when a ':' followed it, and the first pin when not.
	if (it.colon == COLON) {
	    port = it.num;
	    lo = it.lo;
	}
	else
	    lo = it.num;
	if ((lo >= 0) || (it.hi >= 0)) {
	    // `..hi` alone continues from the last pin named; `lo` alone is the
	    // one-pin range lo..lo.
	    ivalue_t hi;
	    ivalue_t p;

	    if (lo < 0)
		lo = last + 1;
	    hi = (it.hi >= 0) ? it.hi : lo;
	    if (hi < lo) {              // `9:5..2` -- descending is a typo
		csp_set_error(st, ERR_NUMBER_RANGE);
		return -1;
	    }
	    for (p = lo; p <= hi; p++) {
		if (k >= (int)alen) {
		    csp_set_error(st, ERR_NUMBER_RANGE);
		    return -1;
		}
		if (set_elem_pin(st, head + k, type, port, p) < 0)
		    return -1;
		k++;
	    }
	    last = hi;
	}
    }
    if (k != (int)alen) {
	csp_set_error(st, ERR_NUMBER_RANGE);   // too few pins for the length
	return -1;
    }
    return 0;
}

//
NOINLINE int csp_parse_variable(csp_rt_t* st, token_t* tv, int ti, size_t n)
{
    variable_param_t d = {0};
    index_t ix;
    int i, r;
    ivalue_t alen = 1;

    // set default values
    d.r.res = 8*sizeof(ivalue_t);
    d.opts.vt = V_INTEGER;

    if (array_splice(st, tv, ti, &n, &alen) < 0)
	return -1;

    if ((r = pmatch(st, tv, ti, n, pat_variable, &d, sizeof(d))) < 0) {
	csp_set_error(st, ERR_SYNTAX);
	return -1;
    }
    if (check_res(st, d.r.res) < 0)
	return -1;
    if ((ix = csp_new_udecl(st,&d.name,DECL_VARIABLE)) == BAD_INDEX)
	return -1;
    i = INDEX(ix);
    ram_decl_at(st,i)->vt = d.opts.vt;
    ram_decl_at(st,i)->res = MAKE_RES(d.r.res);
    ram_decl_at(st,i)->dir = d.opts.dir;
    ram_decl_at(st,i)->va.init = d.init;

    // optional:  bind <buffer> '[' <bit0> ['..' <bit1>] ']'
    // a bound variable is a bit-field view into a buffer (bits, not bytes)
    if ((r < (int)n) && (tv[r].t == WORD) && (tv[r].v.str.len == 4) &&
	(ro_memcmp(ros_bind, tv[r].v.str.ptr, 4) == 0)) {
	index_t bx;
	ivalue_t b0, b1;
	int j = r + 1;
	if ((j >= (int)n) || (tv[j].t != WORD) ||
	    ((bx = csp_lookup_decl(st, &tv[j].v.str)) == BAD_INDEX) ||
	    (decl(st,INDEX(bx),type) != DECL_BUFFER)) {
	    csp_set_error(st, ERR_SYNTAX); return -1;
	}
	j++;
	if ((j >= (int)n) || (tv[j].t != LB) ||
	    (j+1 >= (int)n) || (tv[j+1].t != INT)) {
	    csp_set_error(st, ERR_SYNTAX); return -1;
	}
	j += 2;
	b0 = b1 = tv[j-1].v.val.i;
	if ((j < (int)n) && (tv[j].t == DOTDOT)) {
	    j++;
	    if ((j >= (int)n) || (tv[j].t != INT)) {
		csp_set_error(st, ERR_SYNTAX); return -1;
	    }
	    b1 = tv[j].v.val.i; j++;
	}
	if ((j >= (int)n) || (tv[j].t != RB)) {
	    csp_set_error(st, ERR_SYNTAX); return -1;
	}
	// The range is in BITS and nothing downstream re-checks it: ca.bit/ca.len
	// would wrap, and the heap access trusts the view -- a bind past the end
	// of the buffer wrote outside it. The buffer is the bound.
	if ((b0 < 0) || (b1 < b0) || (b0 > MAX_VIEW_BIT) ||
	    ((b1 - b0) + 1 > MAX_RES_BITS) ||
	    (b1 >= (ivalue_t)decl(st, INDEX(bx), bf.nbytes) * 8)) {
	    csp_set_error(st, ERR_NUMBER_RANGE); return -1;
	}
	ram_decl_at(st,i)->bound  = 1;
	ram_decl_at(st,i)->ca.id  = INDEX(bx);
	ram_decl_at(st,i)->ca.bit = b0;
	ram_decl_at(st,i)->ca.len = MAKE_FIELD_LEN((b1-b0)+1);
	ram_decl_at(st,i)->ca.endian = d.opts.endian;
    }

    // Made LAST so the copies inherit everything decided above.
    return array_replicate(st, i, DECL_VARIABLE, alen);
}

NOINLINE int csp_parse_rule(csp_rt_t* st, const token_t* tv, int ti, size_t n);

// '#' 'local' <name>[':' <size>] [<opt>+] '=' <expr>
//
// A #local BINDS a formula; it is not a variable you assign. So the head is the
// ordinary variable grammar with the '= <const>' cut off, and what follows the
// '=' is compiled as a RULE with an always-true condition, emitted right here.
// That is the whole "prologue": locals must be declared before they are used
// (a forward reference simply does not resolve), so declaration order IS
// evaluation order, and no separate phase is needed.
//
// Its leaf is single-buffered (BUF_F_LOCAL), which is what lets the rules below
// read the value this cycle instead of the previous one.
NOINLINE int csp_parse_local(csp_rt_t* st, token_t* tv, int ti, size_t n)
{
    variable_param_t d = {0};
    index_t ix;
    int i, r, eq = -1, j;

    d.r.res = 8*sizeof(ivalue_t);
    d.opts.vt = V_INTEGER;

    // Find the '=' and let pmatch see only the declaration head. The tail is an
    // EXPRESSION, which P_CONST_S would refuse -- and refusing is right for a
    // #variable, whose initialiser has to be a constant.
    for (j = ti; j < (int)n; j++) {
	if (tv[j].t == EQ) { eq = j; break; }
    }
    if ((eq < 0) || (eq + 1 >= (int)n)) {
	csp_set_error(st, ERR_SYNTAX);      // a local with no formula is nothing
	return -1;
    }
    if ((r = pmatch(st, tv, ti, eq, pat_variable, &d, sizeof(d))) < 0) {
	csp_set_error(st, ERR_SYNTAX);
	return -1;
    }
    if (check_res(st, d.r.res) < 0)
	return -1;
    if ((ix = csp_new_udecl(st,&d.name,DECL_VARIABLE)) == BAD_INDEX)
	return -1;
    i = INDEX(ix);
    ram_decl_at(st,i)->vt = d.opts.vt;
    ram_decl_at(st,i)->res = MAKE_RES(d.r.res);
    ram_decl_at(st,i)->dir = d.opts.dir;
    ram_decl_at(st,i)->local = 1;
    ram_decl_at(st,i)->va.init.i = 0;

    // The formula, as a rule -- but a rule body is `<name> = <expr>`, and what
    // sits between the name and the '=' here is DECLARATION syntax (':8', an
    // option) that pat_body has no grammar for. Move the name down against the
    // '=' and parse from there; the head tokens have already been consumed by
    // the pmatch above, so overwriting one costs nothing. When there is no
    // ':res' the name is already in place and this is a self-assignment.
    tv[eq-1] = tv[ti];
    // The name is already declared, so a #local whose formula mentions ITSELF
    // resolves -- and reads its own previous value, which is defined (if odd)
    // rather than an error: it is the accumulator a plain variable can express.
    st->cs.local_def = (index_t)(i + 1);   // allow the one assignment that defines it
    r = csp_parse_rule(st, tv, eq-1, n);
    st->cs.local_def = 0;
    return r;
}

// Walk `v0, v1, ... }` from `j`, calling nothing per element unless `head` is
// given: with head < 0 it only COUNTS, which is what lets the length be known
// before the declarations that hold the values exist. Parsing twice costs a
// second constant-fold per element and buys an arbitrary list length with no
// temporary array -- the alternative was a fixed cap on how many elements a
// list may have, sized for a stack this also runs on.
NOINLINE static int init_list(csp_rt_t* st, const token_t* tv, int j, size_t n,
			      decl_opts_t opts, int head, int* np)
{
    int nv = 0;

    for (;;) {
	initval_param_t e;
	int r;

	e.opts = opts;
	e.sep  = 0;
	e.init.i = 0;
	if ((r = pmatch(st, tv, j, n, pat_initval, &e, sizeof(e))) < 0) {
	    csp_set_error(st, ERR_SYNTAX);
	    return -1;
	}
	j = r;
	if (head >= 0)
	    ram_decl_at(st, head + nv)->cn.init = e.init;
	nv++;
	if (e.sep == RBRACE)
	    break;
	// A trailing `,` with nothing after it, or a runaway list.
	if (j >= (int)n) {
	    csp_set_error(st, ERR_SYNTAX);
	    return -1;
	}
    }
    *np = nv;
    return j;
}

// Write a param's live value as well as its declaration. Before csp_rt_start
// there are no slots yet -- csp_rt_start seeds them, and applies the ROM
// overrides -- so this is a no-op then and the value arrives that way instead.
NOINLINE static void set_param_value(csp_rt_t* st, index_t di, value_t v)
{
    value_t* iptr;
    value_t* optr;
    if (!st->started)
	return;
    csp_dio_slots(st, MAKE_INDEX(0, di), &iptr, &optr);
    *iptr = *optr = v;
}

// `#param` and `#constant` share this. A param is the same declaration with the
// `local` bit set -- rules may not assign to it, and it is not folded.
NOINLINE static int parse_constant(csp_rt_t* st, token_t* tv, int ti, size_t n,
				   int is_param)
{
    constant_param_t d = {0};
    index_t ix;
    int i, r, nv = 0;
    ivalue_t alen = 1;

    // set default values
    d.r.res = 8*sizeof(ivalue_t);
    d.opts.vt = V_INTEGER;

    if (array_splice(st, tv, ti, &n, &alen) < 0)
	return -1;

    if ((r = pmatch(st, tv, ti, n, pat_constant, &d, sizeof(d))) < 0) {
	csp_set_error(st, ERR_SYNTAX);
	return -1;
    }
    if (check_res(st, d.r.res) < 0)
	return -1;

    // `= { v0, v1, ... }`. Counted first, so a length that disagrees with the
    // list is caught before any declaration is made -- a mistake, not something
    // to pad or truncate silently.
    if (d.list == LBRACE) {
	if (init_list(st, tv, r, n, d.opts, -1, &nv) < 0)
	    return -1;
	if ((alen > 1) && (nv != alen)) {
	    csp_set_error(st, ERR_NUMBER_RANGE);
	    return -1;
	}
	if (alen == 1)
	    alen = nv;              // `#constant A = {..}` -- the list sets it
    }

    // A #param may be re-declared, and that is the whole mechanism for SETTING
    // one: a config file, or a line at the prompt, carries `#param Kp = 9` and
    // it lands on the param that is already there. Three outcomes:
    //
    //   name is free            an ordinary declaration
    //   a RAM param             its cn.init is overwritten -- durable, and it
    //                           is what /save already writes
    //   a ROM-baked param       flash cannot be written, so a RAM SHADOW is
    //                           declared instead; csp_rt_start finds it by name
    //                           and applies it onto the ROM param's slot. The
    //                           shadow rides into EEPROM as any RAM decl does.
    //
    // Anything else -- the name taken by something that is not a param -- is
    // still ERR_ALREADY_DEFINED, which csp_new_udecl reports below.
    if (is_param && (alen == 1) && (d.list != LBRACE)) {
	index_t px = csp_lookup_decl_local(st, &d.name);
	if ((px != BAD_INDEX) &&
	    (decl(st, INDEX(px), type) == DECL_CONSTANT) &&
	    decl(st, INDEX(px), local)) {
	    // The width and type are what any already-compiled rule was built
	    // against, so a re-declaration may not change them.
	    if ((GET_RES(decl(st, INDEX(px), res)) != d.r.res) ||
		(decl(st, INDEX(px), vt) != d.opts.vt)) {
		if (csp_set_error(st, ERR_PARAM_SHAPE))
		    csp_set_err_arg_tstr(st, 0, &d.name);
		return -1;
	    }
	    if (INDEX(px) >= st->rom_nd) {          // RAM: write it in place
		ram_decl_at(st, INDEX(px))->cn.init = d.init;
		set_param_value(st, INDEX(px), d.init);
		return 0;
	    }
	    // ROM: fall through and declare the shadow.
	    if ((ix = csp_new_decl(st, &d.name, DECL_CONSTANT, 0)) == BAD_INDEX)
		return -1;
	    i = INDEX(ix);
	    ram_decl_at(st,i)->vt = d.opts.vt;
	    ram_decl_at(st,i)->res = MAKE_RES(d.r.res);
	    ram_decl_at(st,i)->local = 1;
	    ram_decl_at(st,i)->cn.init = d.init;
	    set_param_value(st, INDEX(px), d.init);
	    return 0;
	}
    }

    if ((ix = csp_new_udecl(st,&d.name,DECL_CONSTANT)) == BAD_INDEX)
	return -1;
    i = INDEX(ix);
    ram_decl_at(st,i)->vt = d.opts.vt;
    ram_decl_at(st,i)->res = MAKE_RES(d.r.res);
    ram_decl_at(st,i)->local = is_param ? 1 : 0;
    ram_decl_at(st,i)->cn.init = d.init;

    if (array_replicate(st, i, DECL_CONSTANT, alen) < 0)
	return -1;
    // Second pass: this one writes. The head is element 0 like any other.
    if (d.list == LBRACE)
	return (init_list(st, tv, r, n, d.opts, i, &nv) < 0) ? -1 : 0;
    return 0;
}

NOINLINE int csp_parse_constant(csp_rt_t* st, token_t* tv, int ti, size_t n)
{
    return parse_constant(st, tv, ti, n, 0);
}

// '#' 'param' <name>[':' <size>] [<opt>*] '=' <const>
//
// A tunable: it reads like a constant and is written from OUTSIDE the program --
// an immediate `> Kp = 7`, a config channel -- never by a rule. The value is
// therefore not foldable, which is the whole difference from #constant at the
// point of use (see push_var).
NOINLINE int csp_parse_param(csp_rt_t* st, token_t* tv, int ti, size_t n)
{
    return parse_constant(st, tv, ti, n, 1);
}


// '#' 'digital' <name> [<iodir>|<pull>] [<port>':']<pin>
NOINLINE int csp_parse_digital(csp_rt_t* st, token_t* tv, int ti, size_t n)
{
    digital_param_t d = {0};
    index_t ix;
    int i, r;
    ivalue_t alen = 1;

    if (array_splice(st, tv, ti, &n, &alen) < 0)
	return -1;

    if ((r = pmatch(st, tv, ti, n, pat_digital, &d, sizeof(d))) < 0) {
	csp_set_error(st, ERR_SYNTAX);
	return -1;
    }
    if (d.opts.dir == 0) d.opts.dir = DIR_IN;

    if ((ix = csp_new_udecl(st, &d.name,DECL_DIGITAL)) == BAD_INDEX)
	return -1;
    i = INDEX(ix);
    ram_decl_at(st,i)->res = MAKE_RES(1);
    ram_decl_at(st,i)->di.pin = d.port_pin.pin;
    ram_decl_at(st,i)->di.port = d.port_pin.port;
    ram_decl_at(st,i)->dir = d.opts.dir;
    ram_decl_at(st,i)->di.pullup = d.opts.pullup;
    ram_decl_at(st,i)->di.pulldown = d.opts.pulldown;

    if (array_replicate(st, i, DECL_DIGITAL, alen) < 0)
	return -1;
    // The copies inherited element 0's pin; give each its own. `r` is where
    // pmatch stopped, which is the first token of the pin spec's tail.
    if (alen > 1)
	return array_pins(st, tv, r, n, i, DECL_DIGITAL, alen,
			  d.port_pin.port, d.port_pin.pin);
    return 0;
}



//'#' 'analog' <name> [':'<size>] [<opt>*]  [<port>':'] <pin>
//   <opt> := 'in' | 'out' | 'inout' | 'pwm' | 'float' | 'signed' | 'unsigned'
NOINLINE int csp_parse_analog(csp_rt_t* st, token_t* tv, int ti, size_t n)
{
    analog_param_t d = {0};
    index_t ix;
    int i, r;
    ivalue_t alen = 1;

    d.r.res = 10;
    d.opts.vt = V_INTEGER;

    if (array_splice(st, tv, ti, &n, &alen) < 0)
	return -1;

    if ((r = pmatch(st, tv, ti, n, pat_analog, &d, sizeof(d))) < 0) {
	csp_set_error(st, ERR_SYNTAX);
	return -1;
    }
    if (d.opts.dir == 0) d.opts.dir = DIR_IN;
    if (check_res(st, d.r.res) < 0)
	return -1;
    if ((ix = csp_new_udecl(st,&d.name,DECL_ANALOG)) == BAD_INDEX)
	return -1;
    i = INDEX(ix);
    ram_decl_at(st,i)->vt = d.opts.vt;
    ram_decl_at(st,i)->res = MAKE_RES(d.r.res);
    ram_decl_at(st,i)->an.pin = d.port_pin.pin;
    ram_decl_at(st,i)->an.port = d.port_pin.port;
    ram_decl_at(st,i)->dir = d.opts.dir;
    ram_decl_at(st,i)->an.pwm = d.opts.pwm;
    ram_decl_at(st,i)->an.endian = d.opts.endian;

    if (array_replicate(st, i, DECL_ANALOG, alen) < 0)
	return -1;
    if (alen > 1)
	return array_pins(st, tv, r, n, i, DECL_ANALOG, alen,
			  d.port_pin.port, d.port_pin.pin);
    return 0;
}



NOINLINE int csp_parse_timer(csp_rt_t* st, token_t* tv, int ti, size_t n)
{
    timer_param_t d = {0};
    index_t tm, tx;
    int i;
    const tstr_t empty = { .ptr = NULL, .len = 0};

    d.init = 0;
    if (pmatch(st, tv, ti, n, pat_timer, &d, sizeof(d)) < 0) {
	csp_set_error(st, ERR_SYNTAX);
	return -1;
    }
    if ((tm = csp_new_udecl(st,&d.name,DECL_TIMER)) == BAD_INDEX)
	return -1;
    tx = csp_new_decl(st,&empty,DECL_VARIABLE,0);
    if (tx != tm+1) {
	csp_set_error(st, ERR_INTERNAL_ERROR);
	return -1;
    }
    i = INDEX(tx);
    ram_decl_at(st,i)->vt = V_UNSIGNED;
    ram_decl_at(st,i)->res = MAKE_RES(32);
    ram_decl_at(st,i)->va.init.u = 0;

    i = INDEX(tm);
    ram_decl_at(st,i)->vt = V_TIMER;
    ram_decl_at(st,i)->tm.fired = 0;
    ram_decl_at(st,i)->tm.init = d.init;
    ram_decl_at(st,i)->tm.period = d.timeout;
    return 0;
}

// '#' 'can' <name>[':'<size>] [<opt>*] <can-bit>
// <opt> := 'in' | 'out' | 'inout' | 'float' | 'signed' | 'unsigned'
//
// <can-bit> :=
//  <frame-id> '[' <bit-pos> ']'
//  <frame-id> '[' <bit-pos> '..' <bit-pos> ']'
//
NOINLINE int csp_parse_field(csp_rt_t* st, token_t* tv, int ti, size_t n)
{
    field_param_t d = {0};
    index_t ix, idx;
    int i, len;

    d.bit0 = d.bit1 = -1;
    d.r.res = 1;  // single bit is default ok?
    if (pmatch(st, tv, ti, n, pat_field_decl, &d, sizeof(d)) < 0) {
	csp_set_error(st, ERR_SYNTAX);
	return -1;
    }
    // The frame must already be declared: a field is a view into it, and the
    // buffer carries the id, the size and the direction.
    if ((idx = csp_lookup_decl(st, &d.frame)) == BAD_INDEX) {
	csp_set_error(st, ERR_VARIABLE_NOT_DECLARED);
	return -1;
    }
    // Any buffer will do. A field is a bit window into storage, and the
    // transport says how that storage reaches the outside world -- which is
    // none of the window's business. Requiring TR_CAN here refused every field
    // over a plain RAM buffer, and did it through ERR_NOT_A_MODULE with no
    // argument set, so the message read "word  not a module". Everything that
    // is actually CAN-specific (arrival flags, sending on change) is already
    // guarded on transport where it happens.
    if (decl(st, INDEX(idx), type) != DECL_BUFFER) {
	if (csp_set_error(st, ERR_NOT_A_BUFFER))
	    csp_set_err_arg_tstr(st, 0, &d.frame);
	return -1;
    }
    if ((ix = csp_new_udecl(st, &d.name, DECL_FIELD)) == BAD_INDEX)
	return -1;
    if ((d.bit0 >= 0) && (d.bit1 >= d.bit0))
	len = (d.bit1 - d.bit0)+1;
    else if ((d.r.res > 0) && (d.bit0 >= 0))
	len = d.r.res;
    else {
	csp_set_error(st, ERR_SYNTAX);
	return -1;
    }
    // ca.bit is 9 bits and ca.len 5, so a wider field or a higher start bit
    // wrapped instead of being refused. setup_field then checks the range
    // against the frame it views.
    if ((d.bit0 > MAX_VIEW_BIT) || (len > MAX_RES_BITS)) {
	csp_set_error(st, ERR_NUMBER_RANGE);
	return -1;
    }

    i = INDEX(ix);
    ram_decl_at(st,i)->res = MAKE_RES(d.r.res); // same as len?
    ram_decl_at(st,i)->vt = d.opts.vt;
    ram_decl_at(st,i)->dir = d.opts.dir;
    ram_decl_at(st,i)->ca.id = INDEX(idx);   // the #buffer decl
    ram_decl_at(st,i)->ca.bit = d.bit0;
    ram_decl_at(st,i)->ca.len = MAKE_FIELD_LEN(len);
    ram_decl_at(st,i)->ca.endian = d.opts.endian;
    // The field inherits its direction from the frame unless it says otherwise:
    // a frame is read or written as a whole, so per-field dir is rarely wanted.
    if (ram_decl_at(st,i)->dir == 0)
	ram_decl_at(st,i)->dir = decl(st, INDEX(idx), dir);
    return 0;
}

// '#' 'buffer' <name> ':' <size> [<opt>*] ['can' <frame-id>]
// Heap-backed storage. Used directly like a variable (whole buffer = value);
// later mapped with bit-field views (#variable X:n bind Buf[a..b], #field).
//
// The size is always in BYTES -- a #buffer is a byte container, whatever its
// transport. One rule for plain and CAN alike (a frame's size is its DLC, also
// bytes). Sub-byte widths are not a buffer's job: use #variable for a masked
// scalar (it carries its own bit width) and #field for a bit-view into a buffer.
//   #buffer Buf:2                  2 bytes (16 bits)
//   #buffer F201:8  in  can 0x201  8 bytes, classic frame
//   #buffer Fbig:64 out can 0x300  64 bytes, CAN FD
NOINLINE int csp_parse_buffer(csp_rt_t* st, token_t* tv, int ti, size_t n)
{
    buffer_param_t d = {0};
    index_t ix;
    uint32_t nbytes;
    int i;

    d.r.res = 8;                 // default 8 bytes (a full classic CAN frame;
				 // plain buffers default the same, for uniformity)
    d.frameid = -1;              // no 'can' clause seen
    d.opts.vt = V_UNSIGNED;      // raw bits -> unsigned by default
    if (pmatch(st, tv, ti, n, pat_buffer, &d, sizeof(d)) < 0) {
	csp_set_error(st, ERR_SYNTAX);
	return -1;
    }
    d.is_can = (d.frameid >= 0);
    // #buffer size is BYTES, always -- one rule for plain and CAN. bf.nbytes (the
    // internal storage width) is that byte count. Cap at 1023 bytes (10 bits)
    // CAN FD is 64.
    nbytes = (uint32_t)d.r.res;
    if ((d.r.res == 0) || (d.r.res > 1023) || (d.is_can && (d.r.res > 64))) {
	csp_set_error(st, ERR_SYNTAX);
	return -1;
    }
    if ((ix = csp_new_udecl(st, &d.name, DECL_BUFFER)) == BAD_INDEX)
	return -1;
    i = INDEX(ix);
    ram_decl_at(st,i)->vt  = d.opts.vt;
    ram_decl_at(st,i)->dir = d.opts.dir;
    ram_decl_at(st,i)->bf.nbytes = nbytes;
    ram_decl_at(st,i)->bf.transport = d.is_can ? TR_CAN : TR_NONE;
    if (d.is_can) {
	value_t fid;
	index_t cx;
	fid.i = d.frameid;
	if ((cx = lookup_const(st, V_INTEGER, fid)) == BAD_INDEX)
	    cx = new_signed_const(st, fid.i);
	ram_decl_at(st,i)->bf.id = cx;
    }
    return 0;
}

//
// lookup lhs in assignement
// var =  oix '.' fld
//      | obj '.' fld
//      | fld
//      | <var> '[' <pos> ']'             // one bit
//      | <var> '[' <pos0> .. <pos1> ']'  // start pos / end pos
//
// FIXME: add part 
//      <var> '.' <part>   part = 'port'|'pin'|'period'...
//
NOINLINE xindex_t lookup_lhs(csp_rt_t* st, const token_t* tv,
			     index_t oix, const pexpr_t* lhs)
{
    xindex_t ix;
    const tstr_t* name;
    index_t mx;
    ivalue_t dn;
    index_t jx;

    if (oix == BAD_INDEX) {
	if (lhs->len == 1) {  // global | module local
	    name = &tv[lhs->pos].v.str;
	    if ((ix = csp_lookup_decl(st,name)) == BAD_INDEX)
		return BAD_INDEX;
	    // Only a member of THIS module is per-instance; a global resolved
	    // from inside the body stays global.
	    if (is_module_local(st, ix))
		ix = MAKE_XINDEX(XOBJ_CURRENT, XIDX(ix));
	}
	else if (lhs->len == 3) {  // obj.field
	    name = &tv[lhs->pos].v.str;
	    if (((oix = csp_lookup_decl(st,name)) == BAD_INDEX) ||
		(decl(st,INDEX(oix),type) != DECL_OBJECT)) {
		if (csp_set_error(st, ERR_OBJECT_NOT_DECLARED)) {
		    csp_set_err_arg_tstr(st, 0, name);
		}
		return BAD_INDEX;
	    }
	    name = &tv[lhs->pos+2].v.str;
	    goto field;
	}
	else if ((lhs->len >= 4) && (tv[lhs->pos+1].t == LB)) {
	    // Buf[pos] / Buf[pos0..pos1] -- byte access on a buffer
	    int j = lhs->pos;
	    ivalue_t p0, p1;
	    name = &tv[j].v.str;
	    if ((ix = csp_lookup_decl(st, name)) == BAD_INDEX) {
		if (csp_set_error(st, ERR_VARIABLE_NOT_DECLARED))
		    csp_set_err_arg_tstr(st, 0, name);
		return BAD_INDEX;
	    }
	    if (decl(st,INDEX(ix),type) != DECL_BUFFER) {
		csp_set_error(st, ERR_SYNTAX);
		return BAD_INDEX;
	    }
	    if (tv[j+2].t != INT) { csp_set_error(st, ERR_SYNTAX); return BAD_INDEX; }
	    p0 = p1 = tv[j+2].v.val.i;
	    if (tv[j+3].t == DOTDOT) {
		if (tv[j+4].t != INT) {csp_set_error(st,ERR_SYNTAX);return BAD_INDEX;}
		p1 = tv[j+4].v.val.i;
	    }
	    return make_buf_view(st, ix, p0*8, (p1+1)*8-1);
	}
	else {
	    csp_set_error(st, ERR_SYNTAX);
	    return BAD_INDEX;
	}
    }
    else if (lhs->len == 1) {  // oix. <field>
	name = &tv[lhs->pos].v.str;
	goto field;
    }
    else {
	csp_set_error(st, ERR_SYNTAX);
	return BAD_INDEX;
    }
    return ix;
field:
    mx = decl(st,INDEX(oix),mq.mx);  // module def
    dn = decl(st,INDEX(mx),md.n);  // number of elements	    
    if ((jx = lookup_decl_in(st, name,
			     INDEX(mx)+1,INDEX(mx)+1+dn))==BAD_INDEX) {
	if (csp_set_error(st, ERR_FIELD_NOT_FOUND)) {
	    csp_set_err_arg_tstr(st, 0, name);
	}
	return BAD_INDEX;
    }
    ix = MAKE_XINDEX(decl(st,INDEX(oix),mq.m), XIDX(jx));  // named object
    return ix;
}



//  part (',' part)* [? cond]
//
//     lhs   op   rhs    ? cond
//  -------------------------
//  print(x)             ? x > 10
//  obj.x    =  10       ? (y > z)
//  x        <- y+z      ? (y < z)
//  x = 1, y = 2         ? (y < z)
//  print(x)            [? true]
//
NOINLINE int asm_rule(csp_rt_t* st, const token_t* tv, size_t n,
		      index_t oix, const rule_body_part_t* part, int np,
		      const pexpr_t* cond)
{
    size_t num;
    int j, k;
    int dst = -1;
    int cnd = -1;
    int cnd2 = -1;

#ifdef DEBUG
    DBG("asm_rule\n");
    DBG("oix = %d\n", oix);
    DBG("np=%d\n", np);
    for (k = 0; k < np; k++) {
	DBG("part[%d]: assign=%d, rhs.len=%d, rhs.pos=%d\n", k,
	    part[k].assign, part[k].rhs.len, part[k].rhs.pos);
    }
    if (cond)
	DBG("cond.len=%d, cond.pos=%d\n", cond->len, cond->pos);
#endif
    // No per-rule State test is emitted anymore. A State-scoped rule (#in /
    // NORMAL+) is gated OUTSIDE its body: sequentially by the OP_INSTATE/NINSTATE
    // block gate (and, for a bare NORMAL+ rule, an OP_RULE.implicit check in
    // csp_eval_rule), and reactively at dispatch by rule_state[ord] (csp_react).
    // csp_csr gives each such rule a State dependency edge from rule_state, so a
    // State change still wakes it -- no folded "secret" EQI in the condition.
    // cnd stays -1 here (no State condition); the changed/user condition follows.

    // dry run (get nvar) union over all <- parts
    st->cs.nvar = 0;
    for (k = 0; k < np; k++) {
	if (part[k].assign == RIMP) {
	    int r;
	    num = part[k].rhs.len;
	    st->cs.rimp = 1;
	    r = csp_parse_const_expr(st, &tv[part[k].rhs.pos], &num, NULL);
	    st->cs.rimp = 0;
	    if (r == 0)
		return -1;
	}
    }
    if (st->cs.nvar) {
	cnd2 = alloc_reg(st);
	if (!asm_LI(st, cnd2, 0))
	    return -1;
	for (k = 0; k < st->cs.nvar; k++) {
	    if (!asm_mem(st, OP_CHG, cnd2, st->cs.var[k]))
		return -1;
	}
    }
    // cnd = state condition
    // cnd2 = changed condition
    if ((cnd >= 0) && (cnd2 >= 0)) {
	if (!asm_AND(st, cnd, cnd, cnd2))
	    return -1;
	free_reg(st, cnd2);
    }
    else if (cnd2 >= 0)
	cnd = cnd2;

    if (cond && ((num = cond->len) > 0)) {
	rentry_t rcond;
	// generate condition
	if (!csp_parse_expr(st, &tv[cond->pos], &num, &rcond))
	    return -1;
	if (!rcond.L) csp_load(st, &rcond);
	if (cnd < 0) {
	    cnd = alloc_reg(st);
	    if (!asm_MOV(st, cnd, rcond.reg))
		return -1;
	}
	else {
	    if (!asm_AND(st, cnd, rcond.reg, cnd))
		return -1;
	}
	free_reg(st, rcond.reg);
    }
    if (cnd < 0) {
	cnd = alloc_reg(st);
	if (!asm_LI(st, cnd, -1))
	    return -1;
    }
    if (!asm_RULE(st, &j, cnd, 0))
	return -1;
    free_reg(st, cnd);
    for (k = 0; k < np; k++) {
	rentry_t rbody;
	xindex_t ix = BAD_INDEX;   // may still name an object
	csp_part_t lpart = PART_VAL;
	pexpr_t lhs;
	// A[<expr>] target: the index register and the array's length, held from
	// where the subscript is compiled to just before the STORE. Arming any
	// earlier would be consumed by the RIGHT side's own load -- every mem
	// instruction goes through asm_seto, and the one-shot belongs to the
	// store. alen_a == 0 means this part has no runtime subscript.
	int      aidx = -1;
	uint16_t alen_a = 0;

	if (part[k].is_unpack) {          // <var> = <buffer>[bits]   (unpack)
	    xindex_t lx = csp_lookup_decl(st, &part[k].obj);
	    if (lx == BAD_INDEX) {
		csp_set_error(st, ERR_SYNTAX);
		return -1;
	    }
	    if ((XOBJ(lx) == XOBJ_GLOBAL) && is_module_local(st, lx))
		lx = MAKE_XINDEX(XOBJ_CURRENT, XIDX(lx));
	    if (dst >= 0) { free_reg(st, dst); dst = -1; }
	    memset(&rbody, 0, sizeof(rbody));
	    rbody.ix = part[k].src_view;  // load from the source sub-view
	    rbody.X  = 1;
	    rbody.vt = decl(st,INDEX(part[k].src_view),vt);
	    if (!coerce_assign(st, lx, &rbody))
		return -1;
	    if (!rbody.L)
		csp_load(st, &rbody);
	    dst = rbody.reg;
	    if (!asm_mem(st, OP_ST, dst, lx))
		return -1;
	    continue;
	}

	if ((num = part[k].rhs.len) == 0)
	    continue;
	if (dst >= 0) {  // only last part value is kept (for NEXT)
	    free_reg(st, dst);
	    dst = -1;
	}
	if (!csp_parse_expr(st, &tv[part[k].rhs.pos], &num, &rbody))
	    return -1;
	// lhs tokens directly precede the assign token
	lhs.pos = 0;
	lhs.len = 0;
	if (part[k].assign != 0) {
	    if (part[k].has_idx) {            // <buf> '[' i0 ['..' i1] ']' op <rhs>
		index_t bx = csp_lookup_decl(st, &part[k].obj);
		ivalue_t lo, hi;
		decl_t bt = (bx==BAD_INDEX)?DECL_NONE:decl(st,INDEX(bx),type);

		// `A[k] = rhs` on an ARRAY. The LEFT side of a rule body is
		// matched by pat_body, not by the expression parser, so a
		// subscript arrives here as idx0 instead of going through the
		// primary that handles it on the read side. A constant index
		// folds to the element's own declaration: no SETOX, and the
		// bounds check lands at compile time.
		if ((bx != BAD_INDEX) && is_subscriptable(bt) &&
		    !part[k].idx_bits && !part[k].has_range &&
		    (csp_array_len(st, INDEX(bx)) > 1)) {
		    ivalue_t alen = (ivalue_t)csp_array_len(st, INDEX(bx));

		    ix = bx;
		    if ((XOBJ(ix) == XOBJ_GLOBAL) && is_module_local(st, ix))
			ix = MAKE_XINDEX(XOBJ_CURRENT, XIDX(ix));

		    if (part[k].idxe.len > 0) {   // A[<expr>] -- runtime index
			rentry_t rix;
			size_t   nix = part[k].idxe.len;
			if (!csp_parse_expr(st, &tv[part[k].idxe.pos], &nix, &rix))
			    return -1;
			if (csp_load(st, &rix) < 0)
			    return -1;
			aidx = rix.reg;
			alen_a = (uint16_t)alen;
		    }
		    else {                        // A[<const>] -- fold to the element
			if ((part[k].idx0 < 0) || (part[k].idx0 >= alen)) {
			    csp_set_error(st, ERR_INDEX_RANGE);
			    return -1;
			}
			ix = MAKE_XINDEX(XOBJ(ix),
					 XIDX(ix) + (index_t)part[k].idx0);
		    }
		    if (!coerce_assign(st, ix, &rbody))
			return -1;
		    goto have_lhs;
		}

		// byte access targets a buffer; pack (idx_bits) also a variable
		if ((bx == BAD_INDEX) || ((bt != DECL_BUFFER) &&
		    !(part[k].idx_bits && (bt == DECL_VARIABLE)))) {
		    csp_set_error(st, ERR_SYNTAX);
		    return -1;
		}
		if (part[k].idx_bits) {       // already bit positions
		    lo = part[k].idx0;
		    hi = part[k].idx1;
		}
		else {                        // byte index -> bit range
		    ivalue_t p0 = part[k].idx0;
		    ivalue_t p1 = part[k].has_range ? part[k].idx1 : p0;
		    lo = p0*8;
		    hi = (p1+1)*8-1;
		}
		if ((ix = make_buf_view(st, bx, lo, hi)) == BAD_INDEX)
		    return -1;
		if (!coerce_assign(st, ix, &rbody))
		    return -1;
	    }
	    else if (part[k].pfld.len > 0) {  // <obj> '.' <fld> '.' <part>
		csp_part_t pt = part_from_tstr(&part[k].pfld);
		pexpr_t of;
		if (pt == PART_LAST) {
		    csp_set_error(st, ERR_SYNTAX);
		    return -1;
		}
		of.pos = part[k].rhs.pos - 6;  // <obj> '.' <fld>
		of.len = 3;
		if ((ix = lookup_lhs(st, tv, oix, &of)) == BAD_INDEX)
		    return -1;
		lpart = pt;
	    }
	    else if (part[k].fld.len > 0) {   // <obj> '.' <fld>  (field or part)
		csp_part_t pt = part_from_tstr(&part[k].fld);
		index_t ox2 = csp_lookup_decl(st, &part[k].obj);
		int is_obj = (ox2 != BAD_INDEX) &&
		    (decl(st,INDEX(ox2),type) == DECL_OBJECT);
		if (!is_obj && (pt != PART_LAST)) { // <var|field> '.' <part>
		    if (oix != BAD_INDEX) {   // object-init: obj is a field
			pexpr_t f;
			f.pos = part[k].rhs.pos - 4;
			f.len = 1;
			if ((ix = lookup_lhs(st, tv, oix, &f)) == BAD_INDEX)
			    return -1;
		    }
		    else {                    // global | module-local var
			ix = ox2;
			if (ix == BAD_INDEX) {
			    if (csp_set_error(st, ERR_VARIABLE_NOT_DECLARED))
				csp_set_err_arg_tstr(st, 0, &part[k].obj);
			    return -1;
			}
			if ((XOBJ(ix) == XOBJ_GLOBAL) && is_module_local(st, ix))
			    ix = MAKE_XINDEX(XOBJ_CURRENT, XIDX(ix));
		    }
		    lpart = pt;
		}
		else {                        // <obj> '.' <fld> op <rhs>
		    lhs.pos = part[k].rhs.pos - 4;
		    lhs.len = 3;
		}
	    }
	    else if (part[k].obj.len > 0) {   // <var> op <rhs>
		lhs.pos = part[k].rhs.pos - 2;
		lhs.len = 1;
	    }
	}
	if (lhs.len > 0) {
	    if ((ix = lookup_lhs(st, tv, oix, &lhs)) == BAD_INDEX)
		return -1;
	    if (!coerce_assign(st, ix, &rbody))
		return -1;
	}
    have_lhs:
	// <var> = <small-int-imm>  (plain value, not reactive) -> single STI,
	// mirroring EQI so the rule lists cleanly (State=OFF, not State=3). dst is
	// a dead register: STI never writes it, but NEXT points at it so the body
	// renders as the assignment.
	if ((ix != BAD_INDEX) && (lpart == PART_VAL) && !rbody.L &&
	    fits_sti((part[k].assign == RIMP) ? OP_STIMP : OP_ST, &rbody)) {
	    dst = alloc_reg(st);
	    if (alen_a) {
		st->cs.arr_reg = (uint8_t)aidx;
		st->cs.arr_len = alen_a;
	    }
	    if (!asm_STI(st, dst, ix, (int8_t)rbody.val.i))
		return -1;
	    if (alen_a)
		free_reg(st, aidx);
	    continue;
	}
	if (!rbody.L)
	    csp_load(st, &rbody);
	dst = rbody.reg;
	if (ix != BAD_INDEX) {
	    if (lpart != PART_VAL) {      // <var> '.' <part> op <rhs>  -> STP
		if (!asm_mem_part(st, OP_STP, dst, ix, lpart))
		    return -1;
	    }
	    else {
		opcode_t op = (part[k].assign == RIMP) ? OP_STIMP : OP_ST;
		// Armed HERE, after the right side has been loaded: its own LD
		// would otherwise consume the one-shot meant for this store.
		if (alen_a) {
		    st->cs.arr_reg = (uint8_t)aidx;
		    st->cs.arr_len = alen_a;
		}
		if (!asm_mem(st, op, dst, ix))
		    return -1;
		if (alen_a)
		    free_reg(st, aidx);
	    }
	}
    }
    if (dst < 0) { // no body value, load TRUE
	dst = alloc_reg(st);
	if (!asm_LI(st, dst, 0))
	    return -1;
    }
    ram_instr_at(st,j)->r.nxt = st->ps.nn - j;  // relative offset
    if (!asm_NEXT(st, dst))
	return -1;
    free_reg(st, dst);
    return 0;
}



// Emit an always-true rule "state_ix = snum" (RULE/body/NEXT shape, like
// asm_rule). Used for the object-init INIT auto-transition to NORMAL.
NOINLINE static int asm_state_set(csp_rt_t* st, xindex_t state_ix, int snum)
{
    reg_t cnd, dst;
    int rpos;

    cnd = alloc_reg(st);
    if (!asm_LI(st, cnd, -1))            // always-true condition
	return -1;
    if (!asm_RULE(st, &rpos, cnd, 0))
	return -1;
    free_reg(st, cnd);
    dst = alloc_reg(st);                 // dead reg for NEXT's body value
    if (!asm_STI(st, dst, state_ix, snum))
	return -1;
    ram_instr_at(st, rpos)->r.nxt = st->ps.nn - rpos;  // skip body when false
    if (!asm_NEXT(st, dst))
	return -1;
    free_reg(st, dst);
    return 0;
}

NOINLINE int csp_parse_object(csp_rt_t* st, token_t* tv, int ti, size_t n)
{
    object_param_t d = {0};
    index_t mx, ix;
    int m, k;
    
    // Parse: # ModName ObjName (Field (=|<-) Expr)*
    if (pmatch(st, tv, ti, n, pat_object, &d, sizeof(d)) < 0) {
	csp_set_error(st, ERR_SYNTAX);
	return -1;
    }

    // Lookup module (global)
    if ((mx = lookup_decl_in(st, &d.mod_name, 0, st->ps.nd)) == BAD_INDEX) {
	if (csp_set_error(st, ERR_MODULE_NOT_DECLARED)) {
	    csp_set_err_arg_tstr(st, 0, &d.mod_name);
	}
	return -1;
    }
    if (decl(st,INDEX(mx),type) != DECL_MODULE) {
	if (csp_set_error(st, ERR_NOT_A_MODULE)) {
	    csp_set_err_arg_tstr(st, 0, &d.mod_name);
	}
	return -1;
    }
    if (st->ps.nq >= MAX_OBJECT_NUM) {
	csp_set_error(st, ERR_TOO_MANY_OBJECTS);
	return -1;
    }
    if ((ix = csp_new_udecl(st, &d.obj_name, DECL_OBJECT)) == BAD_INDEX)
	return -1;

    // Set up object slot. The DECLARATION carries the object number (mq.m); that
    // is the durable record. object[] is a reverse-map cache that csp_rt_start
    // rebuilds, and it does not exist yet -- it is laid out with the other
    // derived tables, which happens after parsing. csp_object_decl covers the
    // gap for anything that runs in between (listing while /pause defers the
    // rebuild).
    ram_decl_at(st, INDEX(ix))->mq.mx = mx;
    m = st->ps.nq + 1;
    ram_decl_at(st, INDEX(ix))->mq.m = m;
    st->ps.nq++;

    DBG("object %s.%s\n", decl_name(st, mx), decl_name(st, ix));    

    // Generate code for the init list. Two kinds:
    //  - reactive (<-) bindings become STANDING rules (must re-evaluate every
    //    cycle as their inputs change).
    //  - static (=, .part=) config/initial values run ONCE: gated on the
    //    object's State == INIT and terminated by State = NORMAL. Writing them
    //    every cycle is wasteful and, for config parts (OP_STP), keeps `anyd`
    //    set forever so the program never idles. The State = NORMAL is a default
    //    the module's own #in INIT may override within the same cycle (its write
    //    lands in DOUT after this one, and last-writer wins at commit).
    {
	// obj.State: State is always the module's first member (module decl + 1).
	xindex_t state_ix = MAKE_XINDEX(m, INDEX(mx) + 1);
	int nstatic = 0;
	int mk = -1;
	reg_t cnd;

	// Emit reactive (<-) bindings as standing rules; compact the static parts
	// to the front of d.inits[] (in place, order preserved) so they can go
	// into ONE grouped rule below.
	for (k = 0; (k < MAX_INITS) && (d.inits[k].obj.len > 0); k++) {
	    if (d.inits[k].assign == RIMP) {          // reactive: standing rule
		rule_body_part_t p = d.inits[k];
		if (asm_rule(st, tv, n, ix, &p, 1, NULL) < 0)
		    return -1;
	    }
	    else {
		if (nstatic != k)
		    d.inits[nstatic] = d.inits[k];
		nstatic++;
	    }
	}

	if (nstatic > 0) {
	    cnd = alloc_reg(st);
	    if (!asm_mem(st, OP_LD, cnd, state_ix))   // load obj.State
		return -1;
	    if (!asm_INSTATE(st, &mk, cnd, 0, 0))     // gate on INIT (snum 0)
		return -1;
	    free_reg(st, cnd);
	    // all static config/init writes in ONE rule (shares one LI/RULE/NEXT)
	    if (asm_rule(st, tv, n, ix, d.inits, nstatic, NULL) < 0)
		return -1;
	    // auto-transition, AFTER the last static init: State = NORMAL (snum 1)
	    if (asm_state_set(st, state_ix, 1) < 0)
		return -1;
	    // patch the gate to skip the whole INIT block when State != INIT
	    ram_instr_at(st, mk)->in.nxt = st->ps.nn - mk;
	}
    }
    return asm_NEW(st, decl(st,INDEX(mx),md.ent), ix);
}

// A bare top-level rule (no explicit #in, not inside a module) runs in the two
// built-in operating states INIT and NORMAL by default -- "NORMAL+". So global
// logic quiesces when the machine enters a SPECIAL state (FAILSAFE, REBOOT, or a
// user state) instead of leaking into it: FAILSAFE stays an island. INIT is
// included so a stateless program -- which sits in INIT forever, with no state
// machine to move it -- runs its rules from cycle 0 with no startup transition.
//
// Only the per-rule condition (State==INIT || State==NORMAL) is folded in (via
// sdefv, see asm_rule) -- NO OP_INSTATE block gate. A per-rule gate would put an
// INSTATE at each rule's reactive entry ip and break csp_react's dispatch; the
// condition alone gates both the sequential and reactive paths correctly. The
// listing suppresses this implicit State test (csp_print.c OP_EQI) so bare rules
// list back bare. Module-body rules keep their ENTER/LEAVE gating (sdef stays -1
// there). Returns 1 if a wrap was applied (caller must clear it after the rule).
NOINLINE static int wrap_normal_plus(csp_rt_t* st)
{
    if ((st->cs.sdef >= 0) || (st->cs.mdef != BAD_INDEX))
	return 0;                       // inside #in or a module: no wrap
    st->cs.sdefv[0] = STATE_INIT;
    st->cs.sdefv[1] = STATE_NORMAL;
    st->cs.n_sdef   = 2;
    st->cs.sdef     = STATE_INIT;          // >= 0 so asm_rule folds the OR condition
    st->cs.rule_implicit = 1;              // mark the OP_RULE for bare listing
    return 1;
}

NOINLINE int csp_parse_rule(csp_rt_t* st, const token_t* tv, int ti, size_t n)
{
    rule_param_t d = {0};
    int np = 0;
    int wrap;

    if (pmatch(st, tv, ti, n, pat_rule, &d, sizeof(d)) < 0) {
	csp_set_error(st, ERR_SYNTAX);
	return -1;
    }
    // A bad guard (after `?`) makes P_EXPR_S fail INSIDE the optional `? <cond>`
    // block; P_OPT then backs off and drops the guard -- silently storing an
    // always-on rule from a typo. Reject it two ways: an undefined name already
    // set a (specific) error, so honour it; a malformed expr (`A &&`) may not, so
    // if a `?` is present but no condition was captured, that is a dropped guard.
    if (st->ps.err != ERR_OK)
	return -1;
    if (d.cond.len == 0) {
	int q;
	for (q = ti; q < (int)n; q++)
	    if (tv[q].t == QUEST) {
		csp_set_error(st, ERR_SYNTAX);
		return -1;
	    }
    }
    while ((np < MAX_BODY_PARTS) && (d.body[np].rhs.len > 0))
	np++;
    wrap = wrap_normal_plus(st);
    if (asm_rule(st, tv, n, BAD_INDEX, d.body, np, &d.cond) < 0)
	return -1;
    if (wrap) {                         // clear the NORMAL+ context (no gate)
	st->cs.sdef   = -1;
	st->cs.n_sdef = 0;
	st->cs.rule_implicit = 0;
    }
    return 0;
}

// '<buffer>' '<<=' <field>... ['?' <cond>]   (frame packing)
//   <field> := <expr> [':' <bits>]   blank separated
// Fields are laid out at ascending bit offsets, each masked to its width.
// Sugar for a sequence of bit-field stores: <buf>[off..off+w-1] = <expr>.
NOINLINE int csp_parse_pack(csp_rt_t* st, token_t* tv, size_t n)
{
    pack_param_t d = {0};
    rule_body_part_t part[MAX_PACK];
    index_t bx;
    int np = 0, off = 0;
    decl_t bt;
    int unpack;
    
    if (pmatch(st, tv, 0, n, pat_pack, &d, sizeof(d)) < 0) {
	csp_set_error(st, ERR_SYNTAX);
	return -1;
    }
    // Same dropped-guard trap as csp_parse_rule: a bad `? <cond>` fails inside
    // the optional block and is silently discarded. Reject it.
    if (st->ps.err != ERR_OK)
	return -1;
    if (d.cond.len == 0) {
	size_t q;
	for (q = 0; q < n; q++)
	    if (tv[q].t == QUEST) {
		csp_set_error(st, ERR_SYNTAX);
		return -1;
	    }
    }
    unpack = (d.op == GTGT);
    bx = csp_lookup_decl(st, &d.buffer);
    bt = (bx == BAD_INDEX) ? DECL_NONE : decl(st,INDEX(bx),type);
    if ((bt != DECL_BUFFER) && (bt != DECL_VARIABLE)) {
	csp_set_error(st, ERR_SYNTAX);
	return -1;
    }
    while ((np < MAX_PACK) && (d.field[np].val.len > 0)) {
	pack_field_t* f = &d.field[np];
	int w = f->bits;
	index_t fx = BAD_INDEX;            // the field's variable, if a single name
	if ((f->val.len == 1) && (tv[f->val.pos].t == WORD))
	    fx = csp_lookup_decl(st, &tv[f->val.pos].v.str);
	if (w <= 0) {                      // default width: variable's resolution
	    if (fx == BAD_INDEX) {
		csp_set_error(st, ERR_SYNTAX);
		return -1;
	    }
	    w = GET_RES(decl(st,INDEX(fx),res));
	}
	memset(&part[np], 0, sizeof(part[np]));
	part[np].assign = EQ;
	if (unpack) {                      // <var> = <buffer>[off..off+w-1]
	    index_t vw;
	    if (fx == BAD_INDEX) {         // unpack target must be a variable
		csp_set_error(st, ERR_SYNTAX);
		return -1;
	    }
	    if ((vw = make_buf_view(st, bx, off, off + w - 1)) == BAD_INDEX)
		return -1;
	    part[np].obj       = tv[f->val.pos].v.str;
	    part[np].is_unpack = 1;
	    part[np].src_view  = vw;
	}
	else {                             // <buffer>[off..off+w-1] = <expr>
	    part[np].obj      = d.buffer;
	    part[np].has_idx  = 1;
	    part[np].idx_bits = 1;
	    part[np].idx0     = off;
	    part[np].idx1     = off + w - 1;
	    part[np].rhs      = f->val;
	}
	off += w;
	np++;
    }
    return asm_rule(st, tv, n, BAD_INDEX, part, np,
		    (d.cond.len > 0) ? &d.cond : NULL);
}

// create (or reuse) a synthetic HEAP sub-view into buffer `parent`, covering
// bits [b0 .. b1]. Used for Buf[pos]/Buf[pos0..pos1]. Parent decl index, start
// bit and length are stashed in the can fields and translated to a VIEW_HEAP
// entry in csp_rt_start (after the parent buffer has been allocated).
NOINLINE index_t make_buf_view(csp_rt_t* st, xindex_t parent,
			       ivalue_t b0, ivalue_t b1)
{
    index_t ix;
    int i;
    index_t pi = XIDX(parent);
    const tstr_t name = { .ptr = NULL, .len = 0 };

    // Both call sites hand us raw byte indices scaled to bits, so this is where
    // Buf[a..b] is bounds-checked: against the buffer, and against what ca.bit
    // and ca.len can hold. Otherwise the slice wrapped and read past the heap.
    if ((b0 < 0) || (b1 < b0) || (b0 > MAX_VIEW_BIT) ||
	((b1 - b0) + 1 > MAX_RES_BITS) ||
	(b1 >= (ivalue_t)decl(st, pi, bf.nbytes) * 8)) {
	csp_set_error(st, ERR_NUMBER_RANGE);
	return BAD_INDEX;
    }
    for (i = 0; i < st->ps.nd; i++) {  // dedup
	if ((ram_decl_at(st,i)->type == DECL_VIEW) &&
	    (ram_decl_at(st,i)->ca.id == pi) &&
	    (ram_decl_at(st,i)->ca.bit == b0) &&
	    (ram_decl_at(st,i)->ca.len == MAKE_FIELD_LEN((b1-b0)+1)))
	    return MAKE_INDEX(0, i);
    }
    if ((ix = csp_new_decl(st,&name,DECL_VIEW,0)) == BAD_INDEX)
	return BAD_INDEX;
    i = INDEX(ix);
    ram_decl_at(st,i)->vt     = V_UNSIGNED;
    ram_decl_at(st,i)->dir    = decl(st,pi,dir);
    ram_decl_at(st,i)->res    = MAKE_RES((b1-b0)+1);
    ram_decl_at(st,i)->ca.id  = pi;
    ram_decl_at(st,i)->ca.bit = b0;
    ram_decl_at(st,i)->ca.len = MAKE_FIELD_LEN((b1-b0)+1);
    ram_decl_at(st,i)->ca.endian = E_NATIVE;
    return ix;
}

// '#' ('disable'|'enable') <rule-range>
//   <rule-range> = <item> (WS <item>)*,  <item> = N | N '-' M
//
// Emits NO instructions: this edits the disable set, it is not part of the
// program. Numbers are stored as typed and only resolved to instruction
// addresses in build_dis_ip, so an edit that renumbers re-resolves on its own.
//
// A number past the last rule is an ERROR, not a note: naming a rule that does
// not exist is far more often a typo than intent. That does mean `#disable 5`
// cannot sit ABOVE the rules in a source file -- which is fine, since counting
// rules that come later is awkward and the line reads better after them.
//
// A RANGE is different: `1-40` on a six-rule program is a sweep, not a claim
// about rule 40, so the top end is clamped and only a range that starts past
// the end is rejected.
//
// The scanner has already turned "1-3" into INT MINUS INT, which is why this
// reads tokens rather than the raw text.
NOINLINE static int csp_parse_disable(csp_rt_t* st, token_t* tv, int ti,
				      size_t n, int off)
{
    int i = ti;
    int nr = (int)csp_n_rules(st);   // current, not the last rebuild's count
    int cap = (nr < MAX_DIS_RULES) ? nr : MAX_DIS_RULES;

    if ((int)n <= ti) {                       // bare "#disable": nothing to do
	csp_set_error(st, ERR_SYNTAX);
	return -1;
    }
    while ((i < (int)n) && (tv[i].t != NEWLINE)) {
	int lo, hi, k;
	if (tv[i].t != INT) {
	    csp_set_error(st, ERR_SYNTAX);
	    return -1;
	}
	lo = hi = (int)tv[i].v.val.i;
	i++;
	if ((i+1 < (int)n) && (tv[i].t == MINUS) && (tv[i+1].t == INT)) {
	    hi = (int)tv[i+1].v.val.i;
	    i += 2;
	}
	if ((lo < 1) || (hi < lo)) {
	    csp_set_error(st, ERR_BAD_RULE_RANGE);
	    csp_set_err_arg_int(st, 0, MAX_DIS_RULES);
	    return -1;
	}
	if (lo > cap) {              // the whole item is past the last rule
	    csp_set_error(st, ERR_NO_SUCH_RULE);
	    csp_set_err_arg_int(st, 0, lo);
	    csp_set_err_arg_int(st, 1, nr);
	    return -1;
	}
	if (hi > cap)
	    hi = cap;                // range overshoot: sweep to the end
	for (k = lo; k <= hi; k++) {
	    if (off) bitset_set(st->dis_rule, k-1);
	    else     bitset_clr(st->dis_rule, k-1);
	}
    }
    st->edited = 1;      // csp_cycle rebuilds, which re-derives dis_ip
    return 0;
}

// '>' command
NOINLINE int csp_parse_immediate(csp_rt_t* st, token_t* tv, int ti, size_t n)
{
    return 0;
}


// Emit the block gate for `#in <states...>` (or the implicit NORMAL+ wrap). One
// LD of State, then an OR-chain: each state but the last is an OP_NINSTATE that
// jumps INTO the block when it matches; the last is an OP_INSTATE that skips the
// block when it does not. So the chain reads "enter if A, else enter if B, else
// enter if C, else skip." The NINSTATE targets (block start) are patched here;
// the INSTATE skip distance is patched at #end. Sets st->cs.sdefv/n_sdef/sdef (used
// by asm_rule to fold the reactive per-rule State condition) and st->cs.in_marker.
NOINLINE static bool_t open_in_block(csp_rt_t* st, const uint8_t* states, int ns,
				     int implicit)
{
    reg_t cnd;
    int npos[MAX_IN_STATES];
    int mk = 0, k, l1;

    if ((ns < 1) || (ns > MAX_IN_STATES))
	return 0;
    cnd = alloc_reg(st);
    if (!asm_mem(st, OP_LD, cnd, st->cs.sx))
	return 0;
    for (k = 0; k < ns - 1; k++)
	if (!asm_NINSTATE(st, &npos[k], cnd, states[k]))
	    return 0;
    if (!asm_INSTATE(st, &mk, cnd, states[ns-1], implicit))
	return 0;
    free_reg(st, cnd);
    l1 = st->ps.nn;                     // block starts right after the chain
    for (k = 0; k < ns - 1; k++)        // NINSTATE jumps forward into the block
	ram_instr_at(st, npos[k])->in.nxt = l1 - npos[k];
    for (k = 0; k < ns; k++)
	st->cs.sdefv[k] = states[k];
    st->cs.n_sdef  = (uint8_t)ns;
    st->cs.sdef    = states[0];
    st->cs.in_marker = mk;
    return 1;
}

// Close the current #in block: patch the terminating OP_INSTATE.nxt to skip the
// whole block on a State mismatch, and clear the compile-time state context.
NOINLINE static void close_in_block(csp_rt_t* st)
{
    ram_instr_at(st, st->cs.in_marker)->in.nxt = st->ps.nn - st->cs.in_marker;
    st->cs.sdef   = -1;
    st->cs.n_sdef = 0;
}

// #in <state> [<state> ...]  -- a block that runs in ANY of the listed states.
// Multiple states OR together (see open_in_block). A single state is the common
// case and emits exactly one OP_INSTATE, byte-identical to the pre-multi format.
NOINLINE int csp_parse_in(csp_rt_t* st, token_t* tv, int ti, size_t n)
{
    uint8_t states[MAX_IN_STATES];
    int ns = 0;
    int i;

    if (tv[ti].t != WORD) return -1;

    if (st->cs.sdef != -1) {   // implicit NORMAL+ wraps close themselves, so an open
	csp_set_error(st, ERR_END_MISMATCH);   // block here is a genuine nested #in
	return -1;
    }
    for (i = ti; (i < (int)n) && (tv[i].t == WORD); i++) {
	int s;
	if (ns >= MAX_IN_STATES) {
	    csp_set_error(st, ERR_SYNTAX);
	    return -1;
	}
	if ((s = lookup_state(st, &tv[i].v.str)) < 0) {
	    csp_set_err_arg_tstr(st, 0, &tv[i].v.str);
	    csp_set_error(st, ERR_STATE_NOT_DECLARED);
	    return -1;
	}
	states[ns++] = (uint8_t)s;
    }
    if (!open_in_block(st, states, ns, 0)) {
	csp_set_error(st, ERR_SYNTAX);
	return -1;
    }
    return 0;
}

// no need to use pattern parser here, yet too simple
NOINLINE int csp_parse_states(csp_rt_t* st, token_t* tv, int ti, size_t n)
{
    // One cursor for the whole statement, so `#states a b c` packs into a
    // single block. Local on purpose -- see add_state for why it must not reach
    // back into a block an earlier line created.
    index_t blk = BAD_INDEX;
    int i;
    for (i = ti; i < n; i++) {
	int j;
	if (tv[i].t != WORD) return -1;
	if ((j = lookup_state(st, &tv[i].v.str)) >= 0)
	    continue; // already installed (no error maybe warning?)
	if (add_state(st, &tv[i].v.str, &blk) < 0)
	    return -1;
    }
    return 0;
}

NOINLINE int csp_parse(csp_rt_t* st, char* str)
{
    token_t tv[MAX_LINE_TOKENS];
    size_t num = MAX_LINE_TOKENS;
    reg_allocator_t alloc;
    int n;

    st->cs.ap = &alloc;
    csp_stack_mark();     // tv[MAX_LINE_TOKENS] is already on the stack here
    while((n = csp_scan_line(st, str, tv, &num)) > 0) {
	int r = -1;
	str += n;
	alloc_init(st->cs.ap);

	if (tv[0].t == NEWLINE)
	    r = 0;
	else if ((tv[0].t == HASH) && (tv[1].t == T_IN)) {
	    r = csp_parse_in(st, tv, 2, num);
	}
	// #field needs no branch of its own: 'field' is not a reserved token, so
	// it arrives as a WORD and find_decl_entry maps it like every other
	// declaration keyword. ('can' still is reserved -- it is the transport
	// option on #buffer -- which is exactly why #field DID need one.)
	// Reserved tokens, so they never reach the WORD branch and
	// get mistaken for a module instantiation.
	else if ((tv[0].t == HASH) &&
		 ((tv[1].t == T_DISABLE) || (tv[1].t == T_ENABLE))) {
	    r = csp_parse_disable(st, tv, 2, num, (tv[1].t == T_DISABLE));
	}
	else if ((tv[0].t == HASH) && (tv[1].t == WORD)) {
	    int i;
	    if ((i = find_decl_entry(tv[1].v.str.ptr,tv[1].v.str.len)) >= 0) {
		// decl_table is indexed BY the keyword token, so `i` IS the
		// dtok. #local maps to DECL_VARIABLE exactly as #variable does
		// -- they differ by a bit, not a type -- so it has to be told
		// apart here rather than in the switch below.
		if (i == D_LOCAL)
		    r = csp_parse_local(st, tv, 2, num);
		// Same reason: #param maps to DECL_CONSTANT exactly as
		// #constant does, so the two are told apart here too.
		else if (i == D_PARAM)
		    r = csp_parse_param(st, tv, 2, num);
		else
		switch(decl_table_code(i)) {
		case DECL_MODULE:
		    r = csp_parse_module(st, tv, 2, num);
		    break;
		case DECL_STATES:  // '#' 'states' WORD ... WORD
		    r = csp_parse_states(st, tv, 2, num);
		    break;
		case DECL_END:
		    r = csp_parse_end(st, tv, 2, num);
		    break;
		case DECL_VARIABLE:
		    r = csp_parse_variable(st, tv, 2, num);
		    break;
		case DECL_CONSTANT:
		    r = csp_parse_constant(st, tv, 2, num);
		    break;
		case DECL_DIGITAL:
		    r = csp_parse_digital(st, tv, 2, num);
		    break;
		case DECL_ANALOG:
		    r = csp_parse_analog(st, tv, 2, num);
		    break;
		case DECL_TIMER:
		    r = csp_parse_timer(st, tv, 2, num);
		    break;
		case DECL_FIELD:
		    r = csp_parse_field(st, tv, 2, num);
		    break;
		case DECL_BUFFER:
		    r = csp_parse_buffer(st, tv, 2, num);
		    break;
		default:
		    r = -1;
		    csp_set_error(st, ERR_SYNTAX);
		    break;
		}
	    }
	    else {
		r = csp_parse_object(st, tv, 1, num);
	    }
	}
	else if (tv[0].t == GT) {
	    r = csp_parse_immediate(st, tv, 1, num);
	}
	else if ((num >= 3) && (tv[0].t == WORD) && (tv[2].t == EQ) &&
		 ((tv[1].t == LTLT) || (tv[1].t == GTGT))) {
	    r = csp_parse_pack(st, tv, num);        // <buf> <<=/>>= <fields>
	}
	else {
	    r = csp_parse_rule(st, tv, 0, num);
	}
	if (r < 0)
	    return -1;
	// AFTER the line, not before: parse_file starts the count at 1, so an
	// error on line N has to leave ps.line at N, not N+1. Incrementing first
	// made every reported line one too high -- the constant off-by-one under
	// the per-comment drift above.
	st->ps.line++;
	num = MAX_LINE_TOKENS;
    }
    return n;
}


#endif /* !CSP_EXEC_ONLY */
