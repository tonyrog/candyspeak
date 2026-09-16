#ifndef __CSP_EXPR_H__
#define __CSP_EXPR_H__

#include "csp.h"

#ifdef __cplusplus
EXTERN_C_BEGIN
#endif

int csp_print_rule(csp_rt_t* st, int i);
// The condition of the #when gate at `gate`, whose instructions start at `from`.
int csp_print_when(csp_rt_t* st, int from, int gate);

#ifdef __cplusplus
EXTERN_C_END
#endif

#endif
