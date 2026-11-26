// The parser: utils/candyspeak.y's LR engine and tables, run over one line of
// tokens, and the helpers the declarations read their parts with -- the
// options, a constant.
#include "csp_parse.h"
#include "csp_strings.h"   // the trigger spellings parse_opts matches
#include <string.h>

// Part of the compiler: an exec-only build has no compiler, so this whole
// translation unit goes with it -- same reasoning and same shape as
// csp_compile.c.
#if !defined(CSP_EXEC_ONLY)

// Map a word to an interrupt trigger, IRQ_NONE if it is not one.
//
// The same shape part_from_tstr has, and for the same reason: these are NAMEs,
// not reserved words. `ready` is a #variable in examples/can_pack.csp and `high`
// and `low` are names anyone would reach for -- reserving them to spell an
// a trigger would cost more than it bought. parse_opts STOPS on a word that is
// not one, so a name in that position is still a name.
NOINLINE trigger_t trig_from_tstr(const tstr_t* s)
{
    switch(s->len) {
    case 3:
	if (ro_memcmp(s->ptr, s_low, 3) == 0)      return IRQ_LOW;
	break;
    case 4:
	if (ro_memcmp(s->ptr, s_both, 4) == 0)     return IRQ_BOTH;
	if (ro_memcmp(s->ptr, s_high, 4) == 0)     return IRQ_HIGH;
	// Not a trigger: the word that says sampling is acceptable. Same lookup
	// so there is one table of option words that are ordinary names.
	if (ro_memcmp(s->ptr, s_soft, 4) == 0)     return IRQ_SOFT;
	break;
    case 5:
	if (ro_memcmp(s->ptr, s_ready, 5) == 0)    return IRQ_READY;
	break;
    case 6:
	if (ro_memcmp(s->ptr, s_rising, 6) == 0)   return IRQ_RISING;
	break;
    case 7:
	if (ro_memcmp(s->ptr, s_falling, 7) == 0)  return IRQ_FALLING;
	break;
    default:
	break;
    }
    return IRQ_NONE;
}

// how to do this be made with pattern?
NOINLINE decl_opts_t parse_opts(csp_rt_t* st, const token_t* tv,
				int* ip, size_t n,
				decl_opts_t opts)
{
    int i = *ip;

    while(i < (int)n) {
	switch(tv[i].t) {
	case T_UNSIGNED: opts.vt=V_UNSIGNED; DBG("UNSIGNED,"); break;
	case T_INTEGER:  opts.vt=V_INTEGER; DBG("INTEGER,"); break;
	case T_FLOAT:    opts.vt=V_FLOAT; DBG("FLOAT,"); break;
	case T_STRING:   opts.vt=V_STRING; DBG("STRING,"); break;	    
	case T_PWM:      opts.pwm = 1; DBG("T_PWM,"); break;
	case T_IN:       opts.dir |= DIR_IN; DBG("IN,"); break;
	case T_OUT:      opts.dir |= DIR_OUT; DBG("OUT,"); break;
	case T_INOUT:    opts.dir |= DIR_INOUT; DBG("INOUT,"); break;
	case T_NATIVE:   opts.endian=E_NATIVE; DBG("NATIVE,"); break;	    
	case T_LITTLE:   opts.endian=E_LITTLE; DBG("LITTLE,"); break;
	case T_BIG:      opts.endian=E_BIG; DBG("BIG,"); break;	    
	case T_PULLUP:   opts.pullup=1; DBG("PULLUP,"); break;
	case T_PULLDOWN: opts.pulldown=1; DBG("PULLDOWN,"); break;
	// An interrupt trigger. A WORD and not a keyword: `ready` is a
	// #variable in examples/can_pack.csp and `high` and `low` are names
	// anyone would reach for, so reserving them would cost more than it
	// buys. A word that is not a trigger ends the option run, exactly as
	// any other word does -- so a name here is still a name.
	case WORD: {
	    trigger_t t;
	    if ((tv[i].v.str.len == 6) &&
		(ro_memcmp(tv[i].v.str.ptr, s_invert, 6) == 0)) {
		opts.invert = 1; DBG("INVERT,");
		break;
	    }
	    t = trig_from_tstr(&tv[i].v.str);
	    if (t == IRQ_NONE) goto done;
	    // `soft` is not a trigger -- it is what the pin will settle for.
	    // Sharing the lookup keeps one table of words that are names.
	    if (t == IRQ_SOFT) { opts.soft = 1; DBG("SOFT,"); }
	    else               { opts.trig = t; DBG("TRIG,"); }
	    break;
	}
	default: goto done;
	}
	i++;
    }
done:
    if (i != *ip) {
	*ip= i;	
	DBG("\n");
    }
    return opts;
}

// A constant expression's value as type vt: 0, or -1 if it cannot be one.
static int const_as(rentry_t* r, uint8_t vt)
{
    if (!r->I)
	return -1;  // not constant
    switch(vt) {
    case V_UNSIGNED:
    case V_INTEGER:
	if (r->vt == V_INTEGER) return 0;
	if (r->vt == V_UNSIGNED) return 0;
	if (r->vt == V_FLOAT) {          // a float is its IEEE bits here
	    r->val.i = op_CVTFI(fv_from_ieee(r->val.u));
	    return 0;
	}
	return -1;
    case V_FLOAT:
	if (r->vt == V_FLOAT) return 0;
	if (r->vt == V_INTEGER) {
	    r->val.u = fv_to_ieee(op_CVTIF(r->val.i));
	    return 0;
	}
	return -1;
    case V_STRING:
	return (r->vt == V_STRING) ? 0 : -1;
    default:
	return -1;
    }
}

// The constant tv[a] .. tv[b-1] as type vt, in *vp: 0, or -1 if it is not
// one (or not all of it is).
NOINLINE int csp_const_val(csp_rt_t* st, const token_t* tv, int a, int b,
			   uint8_t vt, value_t* vp)
{
    size_t num = (b > a) ? b - a : 1;
    rentry_t result;

    if (!csp_parse_const_expr(st, &tv[a], &num, &result))
	return -1;
    if (((int) num != b - a) || (const_as(&result, vt) < 0))
	return -1;
    *vp = result.val;
    return 0;
}

// An initialiser, tv[a] .. tv[b-1]: 1 and its value in *vp if it is a
// constant of type vt, 0 if it is an expression that is not (a #param's
// value, read at INIT), -1 if it is neither.
NOINLINE int csp_init_val(csp_rt_t* st, const token_t* tv, int a, int b,
			  uint8_t vt, value_t* vp)
{
    size_t num = (b > a) ? b - a : 1;
    rentry_t result;

    if (!csp_parse_const_expr(st, &tv[a], &num, &result) ||
	((int) num != b - a))
	return -1;
    if (const_as(&result, vt) < 0)
	return 0;
    *vp = result.val;
    return 1;
}

/* ------------------------------------------------------------ the LR parser
 * utils/candyspeak.y, the grammar the Erlang parser has too. The engine is
 * csp_lr.c, its tables gen/csp_lr_tab.h. A line is parsed as a program of
 * one statement; a value is the interval of tokens a symbol covers.
 */
#define RO RODATA
#define RB(p) ro_byte(p)
#define RW(p) ro_word(p)
#include "csp_lr.c"

enum {
    L_NONE,
#define X(n, s) L_##n,
#include "csp_lr_tokens.def"
#undef X
};

// the spellings, in code order; the words are the ones before WORD
static const char lr_spell[] RODATA =
#define X(n, s) s
#include "csp_lr_tokens.def"
#undef X
    ;
static const uint8_t lr_spell_len[] RODATA = {
#define X(n, s) sizeof(s) - 1,
#include "csp_lr_tokens.def"
#undef X
};

// the code a word has in csp.y: a keyword, or WORD
static int lr_word(const tstr_t* w)
{
    const char* p = lr_spell;
    int c;

    for (c = 1; c < L_WORD; c++) {
	int len = ro_byte(&lr_spell_len[c - 1]);

	// `_' is a hole to the Erlang scanner; here a WORD hole is a name
	if ((c != L_HOLE) && (len == w->len) &&
	    (ro_memcmp(w->ptr, p, len) == 0))
	    return c;
	p += len;
    }
    return L_WORD;
}

// the scanner's token as csp.y has it, 0 if there is none
static int lr_code(tok_t t)
{
    switch (t) {
    case LP: return L_LP;
    case RP: return L_RP;
    case COLON: return L_COLON;
    case HASH: return L_HASH;
    case DOT: return L_DOT;
    case DOTDOT: return L_DOTDOT;
    case LB: return L_LB;
    case RB: return L_RB;
    case LBRACE: return L_LC;
    case RBRACE: return L_RC;
    case INT: return L_INT;
    case FLT: return L_FLT;
    case STR: return L_STR;
    case EXCLAMATION: return L_NOT;
    case TILDE: return L_TILDE;
    case PLUS: return L_PLUS;
    case MINUS: return L_MINUS;
    case ASTERISK: return L_STAR;
    case SLASH: return L_SLASH;
    case PERCENT: return L_PERCENT;
    case LTLT: return L_SHL;
    case GTGT: return L_SHR;
    case LT: return L_LT;
    case LTEQ: return L_LE;
    case GT: return L_GT;
    case GTEQ: return L_GE;
    case EQEQ: return L_EQ;
    case NEQ: return L_NE;
    case AMP: return L_AMP;
    case BAR: return L_BAR;
    case CIRC: return L_CARET;
    case AMPAMP: return L_ANDAND;
    case BARBAR: return L_OROR;
    case EQ: return L_ASSIGN;
    case RIMP: return L_LARROW;
    case COMMA: return L_COMMA;
    case QUEST: return L_QUEST;
    default: return 0;
    }
}

#define LR_IV(a, b) ((lr_val_t) (((a) << 8) | (b)))
#define LR_A(v) IV_A(v)
#define LR_B(v) IV_B(v)

typedef struct {
    csp_stmt_t* sm;
    const token_t* tv;
    int n;          // tokens on the line
    int i;          // the next one
    int la;         // where the lookahead starts
} lr_line_t;

static int lr_next(void* ctx, lr_val_t* v)
{
    lr_line_t* l = (lr_line_t*) ctx;
    const token_t* t;
    int c;

    l->la = l->i;
    if (l->i > l->n)
	return 0;
    if (l->i == l->n) {
	*v = LR_IV(l->i, l->i);
	l->i++;
	return L_EOL;
    }
    t = &l->tv[l->i++];
    if (t->t >= WORD)       // a keyword carries its text, as a WORD does
	c = lr_word(&t->v.str);
    else if ((c = lr_code(t->t)) == 0)
	return -1;
    // `<<=' and `>>=' are two tokens to the scanner
    if (((c == L_SHL) || (c == L_SHR)) && (l->i < l->n) &&
	(l->tv[l->i].t == EQ)) {
	c = (c == L_SHL) ? L_PACK : L_UNPACK;
	l->i++;
    }
    *v = LR_IV(l->la, l->i);
    return c;
}

static lr_val_t lr_reduce(void* ctx, uint8_t act, lr_val_t* v, uint8_t n)
{
    lr_line_t* l = (lr_line_t*) ctx;

    switch (act) {
    // what a line can be: these are reduced once, at the statement
    case A_immediate: case A_module: case A_states: case A_in_states:
    case A_when: case A_else: case A_digital: case A_digital_pins:
    case A_analog_pins: case A_analog: case A_variable: case A_variable_init:
    case A_constant: case A_local_init: case A_local: case A_param:
    case A_param_init: case A_timer: case A_timer_init: case A_field:
    case A_buffer: case A_define: case A_import_name: case A_import_root:
    case A_import_quoted: case A_annotate: case A_annotate_all: case A_end:
    case A_disable: case A_enable: case A_object:
    case A_rule_guarded: case A_rule: case A_pack: case A_unpack:
    case A_pack_guarded: case A_unpack_guarded: {
	uint8_t i;
	l->sm->act = act;
	l->sm->n = n;
	for (i = 0; (i < n) && (i < STMT_SYMS); i++)
	    l->sm->v[i] = v[i];
	break;
    }
    // the assignments, as they come; one too many is the line's error
    case A_assign: case A_assign_prev: case A_assign_none:
	if (l->sm->np < STMT_PARTS) {
	    l->sm->part[l->sm->np].iv = LR_IV(LR_A(v[0]), LR_B(v[n - 1]));
	    l->sm->part[l->sm->np].op =
		(n == 3) ? (uint8_t) LR_A(v[1]) : STMT_NO_OP;
	}
	l->sm->np++;
	break;
    default:
	break;
    }
    if (n == 0)
	return LR_IV(l->la, l->la);
    return LR_IV(LR_A(v[0]), LR_B(v[n - 1]));
}

int csp_lr_line(const token_t* tv, size_t n, csp_stmt_t* sm)
{
    lr_line_t l;
    lr_val_t v;
    int r;

    sm->act = A_COUNT;
    sm->n = 0;
    sm->np = 0;
    l.sm = sm;
    l.tv = tv;
    l.n = (int) n;
    l.i = 0;
    l.la = 0;
    r = lr_parse(lr_next, lr_reduce, &l, &v);
    sm->at = (uint8_t) l.la;
    return r;
}

#endif /* !CSP_EXEC_ONLY */
