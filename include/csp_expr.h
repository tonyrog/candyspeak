#ifndef __CSP_EXPR_H__
#define __CSP_EXPR_H__

#include "csp.h"

#ifdef __cplusplus
EXTERN_C_BEGIN
#endif

int csp_print_rule(csp_rt_t* st, int i);
int csp_rule_binding(csp_rt_t* st, int ri);
int csp_print_binding(csp_rt_t* st, int i);
int csp_rule_defines_out(csp_rt_t* st, int ri);
int csp_print_formula(csp_rt_t* st, int i);
int csp_rule_const_store(csp_rt_t* st, int ri, index_t* member, value_t* v);
// The condition of the #when gate at `gate`, whose instructions start at `from`.
int csp_print_when(csp_rt_t* st, int from, int gate);

#ifdef __cplusplus
EXTERN_C_END
#endif

#endif
