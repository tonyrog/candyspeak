#ifndef __CSP_DUMP_H__
#define __CSP_DUMP_H__

#include <stdio.h>
#include "csp.h"

#ifndef EXTERN_C_BEGIN
#define EXTERN_C_BEGIN  extern "C" {
#define EXTERN_C_END    }
#endif

#ifdef __cplusplus
EXTERN_C_BEGIN
#endif

extern void    csp_dump(FILE*, csp_rt_t* st);
extern void    csp_dump_tokens(FILE* f,token_t* tv, int n);
extern void    csp_dump_variables(FILE* f, csp_rt_t* st);
extern void    csp_dump_state_erl(FILE* f, csp_rt_t* st);
extern void    csp_dump_result_erl(FILE* f, csp_rt_t* st, index_t x);

extern void    csp_print_expr(FILE*, csp_rt_t* st, index_t x);
extern index_t csp_dump_instr(FILE* f, int lev, csp_rt_t* st, int i,char* eot);
extern index_t csp_dump_decl(FILE* f, int lev, csp_rt_t* st, int i, char* eot);

extern void csp_fprint_tag(FILE* f, csp_rt_t* st, index_t n);
extern void csp_print_tag(csp_rt_t* st, index_t n);

#ifdef __cplusplus
EXTERN_C_END
#endif

#endif
