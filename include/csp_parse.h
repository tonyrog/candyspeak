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
	// trigger_t. An interrupt is a property of how the PIN is configured,
	// the same kind of thing pullup is -- which is why it is an option here
	// and not a declaration of its own.
	unsigned trig:3;
	// `soft`: sampling is acceptable on this pin. Without it, a board that
	// cannot arm the interrupt in hardware is falling SHORT of what the
	// program asked, and /state says so; with it, software is the answer
	// that was wanted.
	unsigned soft:1;
	// `invert`: the pin's level is the opposite of the value.
	unsigned invert:1;
    };
} decl_opts_t;

// Parse options from token stream
decl_opts_t parse_opts(csp_rt_t* st, const token_t* tv, int* ip, size_t n,
		       decl_opts_t iopts);
// A word to an interrupt trigger, IRQ_NONE if it is not one.
trigger_t trig_from_tstr(const tstr_t* s);

// Expression reference start pos and length (in tokens)
typedef struct {
    uint8_t pos;
    uint8_t len;
} pexpr_t;

// One assignment of a rule, an object line or a pack, as asm_rule compiles
// it: `<obj>[.<fld>[.<pfld>]][<index>] (=|<-) <rhs>', or a bare <rhs>.
typedef struct {
    tstr_t obj;
    tstr_t fld;
    tstr_t pfld;
    ivalue_t assign;          // EQ, RIMP, 0 for no target
    ivalue_t has_idx;         // a `[...]' on the target
    ivalue_t has_range;       // `[i0..i1]'
    int idx_bits;             // idx0/idx1 are already bit positions (pack)
    int is_unpack;            // <var> = src_view (unpack)
    index_t src_view;
    ivalue_t idx0;
    ivalue_t idx1;
    pexpr_t idxe;             // an index known at run time
    pexpr_t rhs;
} rule_body_part_t;

#define MAX_BODY_PARTS 8      // <body> (',' <body>)* in one rule
#define MAX_INITS 8           // (<field> (=|<-) <expr>)* on one instance
#define MAX_PACK MAX_BODY_PARTS

// A line as utils/candyspeak.y has it: the statement's action and the token
// interval of each symbol it was made of, (a << 8) | b for tv[a] .. tv[b-1].
#include "csp_lr.h"
#include "csp_lr_act.h"

#define STMT_SYMS 8
#define STMT_PARTS 8
#define STMT_NO_OP 0xff
typedef struct {
    uint8_t act;              // A_digital ..., A_COUNT for none
    uint8_t n;                // symbols in v
    uint8_t at;               // the token a syntax error stopped at
    uint8_t np;               // assignments in part
    lr_val_t v[STMT_SYMS];
    // the assignments of a rule, an object line or an immediate, in order:
    // `ref = expr', `ref <- expr' or `expr', and where its operator is
    struct {
	lr_val_t iv;
	uint8_t op;           // the `=' or `<-', STMT_NO_OP for none
    } part[STMT_PARTS];
} csp_stmt_t;

#define IV(a, b) ((lr_val_t) (((a) << 8) | (b)))
#define IV_A(v) ((int) ((v) >> 8))
#define IV_B(v) ((int) ((v) & 0xff))
// symbol k of the statement: where it starts and ends, and if it is empty
#define SM_A(sm, k) IV_A((sm)->v[(k)])
#define SM_B(sm, k) IV_B((sm)->v[(k)])
#define SM_EMPTY(sm, k) (SM_A(sm, k) == SM_B(sm, k))

// 0 and the statement in *sm if csp.y has the line, n tokens of tv; an LR_
// code (csp_lr.h) if not.
int csp_lr_line(const token_t* tv, size_t n, csp_stmt_t* sm);

// The constant tv[a] .. tv[b-1] as type vt (V_INTEGER ...), in *vp: 0, or
// -1 if it is not one.
int csp_const_val(csp_rt_t* st, const token_t* tv, int a, int b,
		  uint8_t vt, value_t* vp);

// An initialiser, tv[a] .. tv[b-1]: 1 and its value if it is a constant of
// type vt, 0 if it is an expression that is not, -1 if it is neither.
int csp_init_val(csp_rt_t* st, const token_t* tv, int a, int b,
		 uint8_t vt, value_t* vp);

#endif // CSP_PARSE_H
