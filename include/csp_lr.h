// The LR engine for utils/candyspeak.y: tables in gen/csp_lr_tab.h (lrgen_c.py),
// actions in gen/csp_lr_act.h.
#ifndef CSP_LR_H
#define CSP_LR_H

#include <stdint.h>

#ifndef LR_VAL
#define LR_VAL uint16_t
#endif
typedef LR_VAL lr_val_t;

#ifndef LR_DEPTH
#define LR_DEPTH 40
#endif

// The next token: its code (tokens.def, 1..), 0 at the end, < 0 on bad
// input; its value in *v.
typedef int (*lr_next_t)(void* ctx, lr_val_t* v);

// A rule reduced: action act (csp_act.h) on the values of its right hand
// side, v[0] .. v[n-1]; what it returns is the value of the left.
typedef lr_val_t (*lr_reduce_t)(void* ctx, uint8_t act, lr_val_t* v, uint8_t n);

#define LR_OK      0
#define LR_SYNTAX  1    // a token the grammar has no place for
#define LR_DEEP    2    // more than LR_DEPTH symbols on the stack
#define LR_LEX     3    // bad input

// 0 and the program's value in *result, or why not.
int lr_parse(lr_next_t next, lr_reduce_t reduce, void* ctx, lr_val_t* result);

extern int lr_peak;     // the deepest the stack got

#endif
