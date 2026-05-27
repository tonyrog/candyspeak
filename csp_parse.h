#ifndef CSP_PARSE_H
#define CSP_PARSE_H

#include "csp.h"
// #include <stddef.h>  // for offsetof

#define csp_offsetof(type, member) ((size_t) &((type *)0)->member)

// Options from parsing (dir, type, etc)
typedef union {
    unsigned bits:16;
    struct PACKED { // 10 bits currently
	unsigned dir:DIR_BITS;        // 2
	unsigned endian:ENDIAN_BITS;  // 2
	unsigned vt:TYPE_BITS;        // 3
	unsigned pwm:1;
	unsigned pullup:1;
	unsigned pulldown:1;
    };
} decl_opts_t;

// Parse options from token stream
decl_opts_t parse_opts(csp_rt_t* st, token_t* tv, int* ip, size_t n,
		       decl_opts_t iopts);

// Expression reference start pos and length (in tokens)
typedef struct {
    uint8_t pos;
    uint8_t len;
} pexpr_t;

// Pattern commands
enum {
    P_END = 0,    // end of pattern
    P_TOK,        // match token: P_TOK, <tok>
    P_INTEGER,    // parse int expr, store: P_INTEGER, <offset>
    P_FLOAT,      // parse int expr, store: P_FLOAT, <offset>    
    P_NUMBER,     // parse num expr, store: P_NUMBER, <opt-offset>, <offset>
    P_STR,        // capture string from WORD: P_STR, <offset>
    P_EXPR,       // capture epxr from sequence: P_EXPR, <offset>    
    P_OPTS,       // parse options: P_OPTS, <offset>
    P_OPT,        // optional: P_OPT, <len>, ...pattern...
    P_ALT,        // alternatives: P_ALT, <n>, <len1>, ...alt1..., <len2>, ...alt2...
    P_REP,        // repeat: P_REP, <len>, ...pattern...
    P_ARRAY,      // setup array: P_ARRAY, <base_offset>, <element_size>
    P_CALL,       // callback: P_CALL, <func_id>
};

// Callback signature: returns 1 on success, 0 on failure
// st = runtime, tv = current token, ti = token index, data = user struct
typedef int (*pmatch_cb_t)(csp_rt_t* st, token_t* tv, int ti, void* data);

// Match pattern against tokens
// Returns: number of tokens consumed, or -1 on mismatch
int pmatch(csp_rt_t* st, token_t* tv, size_t n, const uint8_t* pat, void* data);

// Register callback (up to 8)
void pmatch_set_cb(int id, pmatch_cb_t cb);

#endif // CSP_PARSE_H
