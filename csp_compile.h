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
extern void csp_compile_init(void);
extern int  add_state(csp_rt_t* st, const tstr_t* name);

extern vtype_t call_rtype(uint8_t rtype, uint16_t argcode, int arity);
extern int8_t decl_table_code(int i);
extern value_t eval1(opcode_t op, value_t y);
extern value_t eval2(opcode_t op, value_t y, value_t z);
extern int find_decl_entry(const char* name, int namelen);
extern int find_tok_entry(const char* name, int namelen);
extern uint8_t fn_type(const csp_func_t* fn, int j, int rom);
extern csp_func_fn func_fn(const csp_func_t* fn, int i, int rom);
extern uint8_t func_rtype(const csp_func_t* fn, int i, int rom);
extern index_t lookup_decl_in(csp_rt_t* st, const tstr_t* name, int start, int stop);
extern int mem_fits(csp_rt_t* st, size_t add);
extern int8_t op_table_arity(int i);
extern int8_t op_table_assoc(int i);
extern int8_t op_table_code(int i);
extern int8_t op_table_prec(int i);
extern int8_t op_table_tok(int i);
extern csp_part_t part_from_tstr(const tstr_t* s);

extern void csp_dio_get_part(csp_rt_t* st, index_t ix, value_t* vp, csp_part_t part, dio_t dir);
extern void csp_dio_set_part(csp_rt_t* st, index_t ix, value_t v, csp_part_t part, dio_t dir);
extern int lookup_string(csp_rt_t* st, char* name, int name_len);
extern int new_string(csp_rt_t* st, char* name, int len);

// Shared with the compiler: the function-table accessors and the operator
// stack's marker encoding.
#define FUNC_MARKER_BASE (T_LAST + 1)
#define IS_FUNC_MARKER(op) ((op) >= FUNC_MARKER_BASE)
extern uint8_t func_flags(const csp_func_t* fn, int i, int rom);
#define func_pure(fn,i,rom)   (func_flags((fn),(i),(rom)) & FUNC_PURE)

#define FUNC_MARKER_EP(op)   (((op) >> 8) & 0x0ff)
#define FUNC_MARKER_TIX(op)  (((op) >> 16) & 0xff)
#define MAKE_FUNC_MARKER(tix, pp0) ((FUNC_MARKER_BASE) +  \
				    ((tix)<<16) + ((pp0)<< 8))

#endif
