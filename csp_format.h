#ifndef __CSP_FORMAT_H__
#define __CSP_FORMAT_H__

#include "csp.h"

#ifndef EXTERN_C_BEGIN
#define EXTERN_C_BEGIN  extern "C" {
#define EXTERN_C_END    }
#endif

#ifdef __cplusplus
EXTERN_C_BEGIN
#endif

extern const char* csp_tag(csp_rt_t* st, index_t n);
extern const char* csp_fmt_pindir(uint8_t dir);
extern const char* csp_fmt_pull(csp_rt_t* st, int ix);
extern const char* csp_fmt_pwm(csp_rt_t* st, int ix);
extern const char* csp_fmt_vtype(vtype_t vt);
extern const char* csp_fmt_endian(vendian_t et);
extern const char* csp_format_error(csp_err_t err);

#ifdef __cplusplus
EXTERN_C_END
#endif

#endif
    
