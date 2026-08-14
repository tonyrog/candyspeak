#ifndef CSP_PARSE_H
#define CSP_PARSE_H

#include "csp.h"

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
decl_opts_t parse_opts(csp_rt_t* st, const token_t* tv, int* ip, size_t n,
		       decl_opts_t iopts);

// Expression reference start pos and length (in tokens)
typedef struct {
    uint8_t pos;
    uint8_t len;
} pexpr_t;

// Pattern commands
enum {
    P_END = 0,      // end of pattern
    P_OPT_END,      // end of pattern optional 
    P_ALT_END,      // end of alternative
    P_CHOICE_END,   // end of all alternatives
    P_REP_END,      // end of repetition
    
    P_TOK,        // match token: P_TOK, <tok>
    P_TOK_W,      // match token: P_TOK, <tok>, <offset>
    P_STR,        // capture string from WORD: P_STR, <offset>
    
    P_INTEGER_S,  // parse int expr, store: P_INTEGER, <offset> <set>
    P_FLOAT_S,    // parse float expr, store: P_FLOAT, <offset> <set>
    P_CONST_S,    // parse num expr / string, store: P_CONST_S,<opt-offset>,<offset> <set>
    P_EXPR_S,     // capture expr with stop-set: P_EXPR_S, <offset>, <set>
    P_OPTS,       // parse options: P_OPTS, <offset>
    P_OPT,        // optional: P_OPT, <len>, ...pattern...
    P_CHOICE,     // alternatives: P_CHOICNE, <n>, P_ALT,<len1> P_ALT_END, ...alt1..., <len2>, ...alt2...    P_CHOICE_END,
    P_ALT,        // alternatives: P_ALT, <n>, <len1>, ...alt1..., <len2>, ...alt2...
    P_REP,        // repeat: P_REP, <len>, ...pattern...
    P_ARRAY,      // setup array: P_ARRAY, <base_offset>, <element_size>
    P_PAT,        // sub pattern: P_PAT, pat_id, <offset>
};

// Match pattern against tokens
// Returns: number of tokens consumed, or -1 on mismatch
int pmatch(csp_rt_t* st, const token_t* tv, int ti, size_t n,
	   const uint8_t* pat, void* data, size_t data_size);

// Budget for ALL stop sets together, not per set. The sets are generated
// (csp_stop_sets.h) and the generator fails the build when they outgrow this,
// so a set can no longer come out short at boot -- which used to happen
// silently and showed up as a couple of dozen unrelated parse failures nowhere
// near the pattern that had outgrown the table. Keep this in step with
// MAX_STOP_TOKENS in utils/gen_patterns.erl.
#define MAX_STOP_TOKENS 192
extern int csp_stop_tokens_used(void);

// The PAT_* and STOP_* enums, generated from utils/syntax.terms together with
// the patterns that use them. Stop-set ids are ALLOCATED, one per capture site
// -- the rule that an id may be built only once cannot be broken by hand any
// more, because ids are not written by hand.
#include "csp_pattern_ids.h"
#include "csp_stop_sets.h"

// mark a token as a set pos (resurse)
#define TOK_SET      0x80
#define TOK_SET_MASK 0x7f
#define STOP_SET(x) (TOK_SET | (x))

// Check if token is in stop-set
int stop_set_has(int set_idx, uint8_t tok);

void dump_stop_sets();

// Register a pattern so P_PAT can resolve it. Stop sets are generated, so
// this no longer scans anything; the name stayed because the call sites read
// well.
void scan_pattern(int pat_id, const uint8_t* pat);

#endif // CSP_PARSE_H
