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
int csp_print_rostr(rostring_t s);

// Print a STRING LITERAL without spending RAM on it. A bare literal goes into
// .rodata, which the AVR linker places in .data -- copied to RAM at boot, so
// every csp_print_str("...") costs its own length in RAM forever. The block
// scope static below puts it in flash instead and reads it back with ro_byte;
// same idea as Arduino's F(). Plain C89 (no statement expression), and it
// compiles as C++ too, which the .ino needs.
//
// This is a STATEMENT, not an expression. Use csp_print_str for real char*
// (names, arguments) and csp_print_rostr for a rochar* you already hold.
#define csp_print_lit(lit) \
    do { static rochar s__lit[] RODATA = lit; csp_print_rostr((rostring_t)s__lit); } while(0)
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
