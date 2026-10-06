// The host harness for C generated from a .csp: port/csp_lib_host.c.

#ifndef __CSP_LIB_HOST_H__
#define __CSP_LIB_HOST_H__

#include <stdint.h>

// One scalar of the program, by its runtime name ("N", "m.V"). get reads the
// committed copy, set writes the working one -- where a stimulus row lands in
// port/csp_linux.c.
typedef struct {
    const char* name;
    int32_t (*get)(void);
    void (*set)(int32_t v);
} csp_lib_name_t;

extern const csp_lib_name_t csp_lib_names[];   // the generated file's

extern void csp_lib_setup(void);
extern int  csp_lib_step(uint32_t now, uint32_t* wait);

// Applies the stimulus rows that are due. Called by the generated step where
// the runtime calls cycle_input: after the inputs, before the rules.
extern void csp_lib_host_input(void);

#endif
