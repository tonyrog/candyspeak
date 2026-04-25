#ifndef __CSP_DUMP_H__
#define __CSP_DUMP_H__

#include <stdio.h>
#include "csp.h"

#ifdef __cplusplus
extern "C" {
#endif

extern void    csp_dump(FILE*, csp_rt_t* st);
extern void    csp_print_expr(FILE*, csp_rt_t* st, index_t x);

#ifdef __cplusplus
}
#endif

#endif
