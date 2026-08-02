#ifndef __CSP_TOK_H__
#define __CSP_TOK_H__

// The operator/token table. Shared by the compiler and the disassembler; the
// evaluator does not use it. See csp_tok.c.
#include "csp.h"

// Operator associativity, and the row builders for the three RODATA op tables.
#define LEFT -1
#define RIGHT 1
#define NO    0

// Row builders for the three RODATA op tables. Shared because the tables now
// live in three files -- tok_table here, decl_table in csp_compile.c, op_info
// in csp_rt.c (the evaluator reads its arity).
#define CSTRLEN(str) (sizeof((str))-1)

#define TOK_ENT(o,c,n) \
    [(o)] = { .tok=(o),.code=(c),.name=(rostring_t)(n),.namelen=CSTRLEN((n)),.arity=-1,.prec=-1,.assoc=NO }

#define INSTR_ENT(o,c,n,a,p,s) \
    [(o)] = { .tok=(o),.code=(c),.name=(rostring_t)(n),.namelen=CSTRLEN((n)),.arity=(a),.prec=(p),.assoc=(s) }

#define DECL_ENT(o,c,n) \
    [(o)] = { .tok=(o),.code=(c),.name=(rostring_t)(n),.namelen=CSTRLEN((n)),.arity=-1,.prec=-1,.assoc=NO }

extern const op_entry_t tok_table[];
extern int find_op_entry(const op_entry_t* tab, int size,
			 const char* name, int namelen);
extern int find_tok_entry(const char* name, int namelen);
extern int8_t op_table_tok(int i);
extern int8_t op_table_arity(int i);
extern int8_t op_table_code(int i);
extern int8_t op_table_prec(int i);
extern int8_t op_table_assoc(int i);
extern rostring_t op_table_name(int i);

#endif
