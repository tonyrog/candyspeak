#ifndef __CSP_PRINT_H__
#define __CSP_PRINT_H__

#include "csp.h"

#ifdef __cplusplus
EXTERN_C_BEGIN
#endif

void* csp_set_file_output(void* f);
int csp_will_output(void);

// platform print functions
int csp_print_char(char c);
int csp_print_str(const char* s);
#if defined(__AVR__)
int csp_print_str_P(rochar* s);  // PROGMEM string
#endif
int csp_print_int(ivalue_t v);
int csp_print_uint(uvalue_t v);
int csp_print_uintw(uvalue_t v, int nw);
int csp_print_float(fvalue_t v);
int csp_print_hex(uvalue_t v);
int csp_println(void);
void csp_flush(void);
int csp_print_value(csp_rt_t* st, vtype_t vt, value_t val);
int csp_print_rule(csp_rt_t* st, int i);

#ifdef __cplusplus
EXTERN_C_END
#endif

#endif
