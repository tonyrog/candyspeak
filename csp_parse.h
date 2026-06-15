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
    P_NUMBER_S,   // parse num expr, store: P_NUMBER,<opt-offset>,<offset> <set>
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
	   const uint8_t* pat, void* data);

// Stop-set functions for P_EXPR_S
#define MAX_STOP_TOKENS 128

// Named stop-sets - used in xxx_pat definitions
enum {
    STOP_NONE = 0,       // empty/invalid placeholder
    STOP_OPTS = 1,       // fixed set containing all OPTION tokens
    STOP_RULE_BODY,      // <body> ? <cond> - stops at QUEST, NEWLINE
    STOP_RULE_COND,      // ? <cond> - stops at NEWLINE
    // Add more as needed:
    STOP_RES,           // [:res]
    STOP_PORT,
    STOP_PIN1,
    STOP_PIN2,
    STOP_VAR_INIT,      // #variable x = <init>
    STOP_CONST_INIT,    // #variable x = <init>
    STOP_TIMER_TMO,
    STOP_TIMER_INIT,
    STOP_CAN_FRAMEID,
    STOP_CAN_BIT0,
    STOP_CAN_BIT1,
    STOP_CAN_BIT00,
    STOP_OBJECT_INIT_RHS,
    // P_PAT continuation sets (built from followers)
    STOP_RULE_BODY_CONT,    // PAT_BODY in pat_rule (both sites)
    STOP_VAR_RES_CONT,      // PAT_RES in pat_variable
    STOP_CONST_RES_CONT,    // PAT_RES in pat_constant
    STOP_DIGITAL_PP_CONT,   // PAT_PORT_PIN in pat_digital
    STOP_ANALOG_RES_CONT,   // PAT_RES in pat_analog
    STOP_ANALOG_PP_CONT,    // PAT_PORT_PIN in pat_analog
    STOP_CAN_RES_CONT,      // PAT_RES in pat_can
    STOP_BUFFER_RES_CONT,   // PAT_RES in pat_buffer
    STOP_BODY_IDX0,         // first index in body lhs  Buf[idx0 ..]
    STOP_BODY_IDX1,         // second index in body lhs Buf[.. idx1]
    NUM_STOP_SETS
};

enum {
    PAT_MODULE,
    PAT_END,
    PAT_RES,
    PAT_PORT_PIN,    
    PAT_VARIABLE,
    PAT_CONSTANT,
    PAT_DIGITAL,
    PAT_ANALOG,
    PAT_TIMER,
    PAT_CAN,
    PAT_BUFFER,
    PAT_OBJECT,
    PAT_BODY,
    PAT_RULE,
    NUM_PAT
};

// mark a token as a set pos (resurse)
#define TOK_SET      0x80
#define TOK_SET_MASK 0x7f
#define STOP_SET(x) (TOK_SET | (x))

// Check if token is in stop-set
int stop_set_has(int set_idx, uint8_t tok);

// Initialize stop-sets storage (call first from csp_rt_init)
void init_stop_sets(void);
void dump_stop_sets();

// Scan entire pattern, build stop-sets for all P_EXPR_S found
// Call patterns in enum order
void scan_pattern(int pat_id, const uint8_t* pat);

#endif // CSP_PARSE_H
