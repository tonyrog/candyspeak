// The host harness for C generated from a .csp: port/csp_lib_host.c.

#ifndef __CSP_LIB_HOST_H__
#define __CSP_LIB_HOST_H__

#include <stdint.h>
#include "csp_lib.h"

// The program's scalars, by runtime name ("N", "m.V", "m.q.V"), WITHOUT a
// function per name: a module lists its fields once and has one getter and
// one setter that switch on the field number; an instance is its path and its
// part of each side. "m.V" is instance "m", field "V" of its module; Main is
// the instance "".
//
// get reads the committed copy, set writes the working one -- where a
// stimulus row lands in port/csp_linux.c. A #param has one copy, and both go
// there. A #local has no name here: it is the module's own, and the
// translation is free to keep it in a C variable.
typedef struct csp_lib_inst_s csp_lib_inst_t;

typedef struct {
    uint16_t nfield;
    const char* const* field;
    int32_t (*get)(const csp_lib_inst_t* x, uint16_t f);
    void (*set)(const csp_lib_inst_t* x, uint16_t f, int32_t v);
} csp_lib_module_t;

struct csp_lib_inst_s {
    const char* name;
    const csp_lib_module_t* mod;
    void* in;     // the committed copy (csp_in's part)
    void* out;    // the working copy
    void* par;    // csp_par's part, or 0
};

extern const csp_lib_inst_t csp_lib_insts[];   // the generated file's, ends at name 0

// Applies the stimulus rows that are due. Called by the generated step where
// the runtime calls cycle_input: after the inputs, before the rules.
extern void csp_lib_host_input(void);

#endif
