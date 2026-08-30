#ifndef __CSP_COMPILE_H__
#define __CSP_COMPILE_H__

// The seam between the RUNTIME and the COMPILER (csp_compile.c: tokenizer,
// expression compiler, declaration parsers, instruction emitter).
//
// It is one-directional, and that is the point: the compiler reaches into the
// runtime -- it emits into the same arena the runtime executes from -- but
// nothing in the runtime calls into the compiler. Measured when the two were
// separated: 17 symbols cross this way, 0 the other. Anything that would need
// to cross back belongs on this side of the line, not the other.
//
// These are runtime internals the compiler needs; everything else it uses is
// already public in csp.h.

#include "csp.h"

// The .ino is C++ and includes this through the driver, so every declaration
// below needs C linkage or the link fails on a mangled name -- which is exactly
// how `csp_cstate()` came out undefined on a mega. csp_dump.h has had these
// guards all along; this header did not need them until a driver started
// calling into it.
#ifndef EXTERN_C_BEGIN
#define EXTERN_C_BEGIN  extern "C" {
#define EXTERN_C_END    }
#endif
#ifdef __cplusplus
EXTERN_C_BEGIN
#endif

// Character classes -- the tokenizer needs them, and so does the command
// splitter in csp_repl.c. No <ctype.h>: locale-dependent, and it is a table
// lookup on a target where these are three comparisons.
// CTYPE
#define ISDIGIT(c) (((c) >= '0') && ((c) <= '9'))
#define ISUPPER(c) (((c) >= 'A') && ((c) <= 'Z'))
#define ISLOWER(c) (((c) >= 'a') && ((c) <= 'z'))
#define ISXUPPER(c) (((c) >= 'A') && ((c) <= 'F'))
#define ISXLOWER(c) (((c) >= 'a') && ((c) <= 'f'))
#define ISXDIGIT(c) (ISDIGIT((c)) || ISXUPPER((c)) || ISXLOWER((c)))
#define ISALPHA(c) (ISUPPER((c)) || ISLOWER((c)))

typedef struct PACKED {
    rostring_t name;   // opcode name (RODATA)
    uint8_t arity;     // number of args
    uint8_t rtype;     // return type
    uint16_t argtypes; // instruction argument types
} op_info_t;

extern const op_info_t op_info[] RODATA;

extern const char* csp_opcode_name(opcode_t op);
extern uint8_t csp_opcode_rtype(opcode_t op);
extern uint8_t csp_opcode_arity(opcode_t op);

// Compiler-side one-time init (stop-sets + declaration patterns).
// `blk` is the caller's cursor over the states block being filled; set it to
// BAD_INDEX before the first name of a statement. See the definition.
extern int  add_state(csp_rt_t* st, const tstr_t* name, index_t* blk);

extern vtype_t call_rtype(uint8_t rtype, uint16_t argcode, int arity);
NOINLINE int eval_op(csp_rt_t* st, int n, csp_instr_t ci, int* leave);
extern uint8_t fn_type(const csp_func_t* fn, int j, int rom);
extern csp_func_fn func_fn(const csp_func_t* fn, int i, int rom);
extern uint8_t func_rtype(const csp_func_t* fn, int i, int rom);
extern index_t lookup_decl_in(csp_rt_t* st, const tstr_t* name, int start, int stop);
extern int mem_fits(csp_rt_t* st, size_t add);
extern csp_part_t part_from_tstr(const tstr_t* s);
// The compiler's state, to hand to csp_rt_init. A driver that wants a node
// which only runs images passes NULL there instead and never calls this.
extern csp_cstate_t* csp_cstate(void);
// timeout(T) compiles to OP_TMO; the compiler needs the same answer for an
// immediate `> timeout(T)`, which has no instruction stream to run.
extern int csp_timer_fired(csp_rt_t* st, index_t ix);

extern void csp_dio_get_part(csp_rt_t* st, index_t ix, value_t* vp, csp_part_t part, dio_t dir);
extern void csp_dio_set_part(csp_rt_t* st, index_t ix, value_t v, csp_part_t part, dio_t dir);
extern int lookup_string(csp_rt_t* st, char* name, int name_len);
extern int new_string(csp_rt_t* st, char* name, int len);

// Shared with the compiler: the function-table accessors and the operator
// stack's marker encoding.
// Two marker kinds now, so the tag lives in the LOW BYTE and the payload above
// it. IS_MARKER is the "acts like LP" test -- nothing reduces past either kind --
// and the two IS_*_MARKER tests say which one closed. A plain token is < T_LAST,
// so it can never look like a marker.
#define FUNC_MARKER_BASE (T_LAST + 1)
#define ARR_MARKER_BASE  (T_LAST + 2)
#define IS_MARKER(op)      ((op) >= FUNC_MARKER_BASE)
#define IS_FUNC_MARKER(op) (((op) & 0xff) == FUNC_MARKER_BASE)
#define IS_ARR_MARKER(op)  (((op) & 0xff) == ARR_MARKER_BASE)
extern uint8_t func_flags(const csp_func_t* fn, int i, int rom);
#define func_pure(fn,i,rom)   (func_flags((fn),(i),(rom)) & FUNC_PURE)

#define FUNC_MARKER_EP(op)   (((op) >> 8) & 0x0ff)
#define FUNC_MARKER_TIX(op)  (((op) >> 16) & 0xff)
#define MAKE_FUNC_MARKER(tix, pp0) ((FUNC_MARKER_BASE) +  \
				    ((tix)<<16) + ((pp0)<< 8))

// An array marker carries the array itself, so no side stack is needed: bits
// 16..30 are the declaration index (DECL_BITS is 15) and bit 31 is the
// CURRENT/global selector. The LENGTH is not stored -- csp_array_len recovers it
// at `]` for the price of a compile-time scan.
#define ARR_MARKER_EP(op)   (((op) >> 8) & 0x0ff)
#define ARR_MARKER_IX(op)   (((op) >> 16) & 0x7fff)
#define ARR_MARKER_CUR(op)  (((op) >> 31) & 1)
#define MAKE_ARR_MARKER(cur, ix, pp0) ((ARR_MARKER_BASE) +	\
				       ((uint32_t)(cur)<<31) +		\
				       ((uint32_t)(ix)<<16) + ((pp0)<<8))

#ifdef __cplusplus
EXTERN_C_END
#endif

#endif
