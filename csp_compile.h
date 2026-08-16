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

#endif
