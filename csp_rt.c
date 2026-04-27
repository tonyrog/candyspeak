// parse and eval

#ifndef CSP_EMBEDDED
#include <stdio.h>
#endif

#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>

#include "csp.h"

#define CAT_HELPER2(x,y) x ## y
#define CAT2(x,y) CAT_HELPER2(x,y)

// convert integer to -1 if y != 0  0 otherwise
#define BOOL(y) (-((y)!=0))

// assoc
#define LEFT -1
#define RIGHT 1
#define NO    0
// func

#define TOK_ENT(o,c,n) \
    [(o)] = { .tok=(o),.ttype=TOKT_TOKEN,.code=(c),.name=(n),.name_len=strlen((n)),.arity=-1,.prec=-1,.assoc=NO }

#define INSTR_ENT(o,c,n,a,p,s) \
    [(o)] = { .tok=(o),.ttype=TOKT_INSTR,.code=(c),.name=(n),.name_len=strlen((n)),.arity=(a),.prec=(p),.assoc=(s) }

#define DECL_ENT(o,c,n) \
    [(o)] = { .tok=(o),.ttype=TOKT_DECL,.code=(c),.name=(n),.name_len=strlen((n)),.arity=-1,.prec=-1,.assoc=NO }

const op_entry_t op_table[] = {
    TOK_ENT(NONE,OP_NOP,"\0"),
    // leaf
    DECL_ENT(MODULE,DECL_MODULE,"module"),
    DECL_ENT(END,DECL_END, "end"),
    DECL_ENT(CONSTANT,DECL_CONSTANT,"constant"),
    DECL_ENT(VARIABLE,DECL_VARIABLE,"variable"),
    DECL_ENT(DIGITAL,DECL_DIGITAL,"digital"),
    DECL_ENT(ANALOG,DECL_ANALOG,"analog"),
    DECL_ENT(TIMER,DECL_TIMER,"timer"),
    DECL_ENT(CAN,DECL_CAN,"can"),
    // node - unary
    INSTR_ENT(EXCLAMATION,OP_NOT,"!",1,105,RIGHT),
    INSTR_ENT(TILDE,OP_INV,"~",1,105,RIGHT),
    INSTR_ENT(MINUS1,OP_NEG,"-",1,105,RIGHT),
    INSTR_ENT(PLUS1,OP_POS,"+",1,105,RIGHT),
    // node - binary
    INSTR_ENT(PLUS,OP_ADD,"+",2,90,LEFT),
    INSTR_ENT(MINUS,OP_SUB,"-",2,90,LEFT),
    INSTR_ENT(ASTERISK,OP_MUL,"*",2,100,LEFT),
    INSTR_ENT(SLASH,OP_DIV,"/",2,100,LEFT),
    INSTR_ENT(PERCENT,OP_REM,"%",2,100,LEFT),
    INSTR_ENT(LTLT,OP_SLA,"<<",2,80,LEFT),
    INSTR_ENT(GTGT,OP_SRA,">>",2,80,LEFT),
    INSTR_ENT(LT,OP_LT,"<",2,70,LEFT),
    INSTR_ENT(LTEQ,OP_LTE,"<=",2,70,LEFT),
    INSTR_ENT(GT,OP_GT,">",2,70,LEFT),
    INSTR_ENT(GTEQ,OP_GTE,">=",2,70,LEFT),
    INSTR_ENT(EQEQ,OP_EQEQ,"==",2,60,LEFT),
    INSTR_ENT(NEQ,OP_NEQ,"!=",2,60,LEFT),
    INSTR_ENT(AMP,OP_AND,"&",2,50,LEFT),
    INSTR_ENT(CIRC,OP_XOR,"^",2,40,LEFT),
    INSTR_ENT(BAR,OP_OR,"|",2,30,LEFT),
    INSTR_ENT(AMPAMP,OP_ANDAND,"&&",2,20,LEFT),
    INSTR_ENT(BARBAR,OP_OROR,"||",2,10,LEFT),
    INSTR_ENT(EQ,OP_EQ,"=",2,5,NO),
    INSTR_ENT(COMMA,OP_COMMA,",",2,2,NO),
    INSTR_ENT(QUEST,OP_RULE,"?",-1,-1,NO),

    // OP_ENTER: y=<num-instr>, z=DECL:module-index
    INSTR_ENT(ENTER,OP_ENTER,"enter",-1,-1,NO),
    // OP_ENTER: y=<num-instr>, z=DECL:module-index
    INSTR_ENT(LEAVE,OP_LEAVE,"leave",-1,-1,NO),
    // OP_NEW: y=INSTR:enter-index, z=DECL:mod-index
    INSTR_ENT(NEW,OP_NEW,"new",-1,-1,NO),
    // functions are now looked up via csp_lookup_func() + csp_builtin_funcs[]

    // keywords
    TOK_ENT(PULLUP,OP_NOP,"pullup"),
    TOK_ENT(PULLDOWN,OP_NOP,"pulldown"),
    TOK_ENT(RESOLUTION,OP_NOP,"resolution"),
    TOK_ENT(IN,OP_NOP,"in"),
    TOK_ENT(OUT,OP_NOP,"out"),
    TOK_ENT(INOUT,OP_NOP,"inout"),
    TOK_ENT(PWM,OP_NOP,"pwm"),
    TOK_ENT(FLOAT,OP_NOP,"float"),
    TOK_ENT(INTEGER,OP_NOP,"integer"),
    TOK_ENT(UNSIGNED,OP_NOP,"unsigned"),
    TOK_ENT(STRING,OP_NOP,"string"),
    TOK_ENT(LITTLE,OP_NOP,"little"),
    TOK_ENT(BIG,OP_NOP,"big"),
    
    // tokens
    TOK_ENT(LP,OP_NOP,"("),
    TOK_ENT(RP,OP_NOP,")"),
    TOK_ENT(HASH,OP_NOP,"#"),
    TOK_ENT(DOT,OP_NOP,"."),
    TOK_ENT(COLON,OP_NOP,":"),
    TOK_ENT(LB,OP_NOP,"["),
    TOK_ENT(RB,OP_NOP,"]"),
    TOK_ENT(INT,OP_NOP,""),
    TOK_ENT(FLT,OP_NOP,""),
    TOK_ENT(WORD,OP_NOP,""),
    TOK_ENT(NEWLINE,OP_NOP,"\n"),
    // eot
    TOK_ENT(LAST,OP_NOP,"<last>")
};

// Helper functions for builtin ops
static inline ivalue_t imax(ivalue_t a, ivalue_t b)
{
    return (a > b) ? a : b;
}

static inline ivalue_t imin(ivalue_t a, ivalue_t b)
{
    return (a < b) ? a : b;
}

static inline ivalue_t iabs(ivalue_t a)
{
    return (a < 0) ? -a : a;
}

static inline ivalue_t isign(ivalue_t a)
{
    return (a < 0) ? -1 : (a ? 1 : 0);
}

// Built-in function implementations - args are pre-evaluated
static ivalue_t fn_min(csp_rt_t* st, value_t* args, uint8_t nargs) {
    (void)st; (void)nargs;
    return imin(args[0].i, args[1].i);
}

static ivalue_t fn_max(csp_rt_t* st, value_t* args, uint8_t nargs) {
    (void)st; (void)nargs;
    return imax(args[0].i, args[1].i);
}

static ivalue_t fn_abs(csp_rt_t* st, value_t* args, uint8_t nargs) {
    (void)st; (void)nargs;
    return iabs(args[0].i);
}

static ivalue_t fn_sign(csp_rt_t* st, value_t* args, uint8_t nargs) {
    (void)st; (void)nargs;
    return isign(args[0].i);
}

// RAW_INDEX functions - receive instruction pointer
static ivalue_t fn_timeout_raw(csp_rt_t* st, csp_instr_t* ip) {
    index_t ty = ip->z;
    ivalue_t tmo = BOOL(!st->decl[INDEX(ty)].tm.running);
    return tmo;
}

static ivalue_t fn_print_raw(csp_rt_t* st, csp_instr_t* ip) {
    index_t ix = ip->z;
    int vi = st_index(st, ix);
    if (IS_DECL(ix)) {
	return csp_print_value(st, st->decl[INDEX(ix)].vt, st->dval[vi]);
    }
    else {
	return csp_print_value(st, st->instr[INDEX(ix)].vt, st->xval[vi]);
    }
}

static ivalue_t fn_tick(csp_rt_t* st, value_t* args, uint8_t nargs) {
    (void)st; (void)args; (void)nargs;
    return csp_time_ms();
}

static ivalue_t fn_cycle(csp_rt_t* st, value_t* args, uint8_t nargs) {
    (void)args; (void)nargs;
    return st->cycle;
}

static ivalue_t fn_pln(csp_rt_t* st, value_t* args, uint8_t nargs) {
    (void)args; (void)nargs;
    switch(nargs) {
    case 0:
	printf("PLN()\n");
	break;
    case 1:
	printf("PLN(%d)\n", args[0].i);
	break;
    case 2:
	printf("PLN(%d,%d)\n", args[0].i, args[1].i);
	break;
    case 3:
	printf("PLN(%d,%d,%d)\n", args[0].i, args[1].i, args[2].i);
	break;
    case 4:
	printf("PLN(%d,%d,%d,%d)\n", args[0].i, args[1].i,
	       args[2].i, args[3].i);
	break;
    default:
	return 0;
    }
    return 1;
}


// Built-in function table 
const csp_func_t csp_builtin_funcs[] = {
    { "",        0, 0, 0,              NULL },
    { "min",     3, 2, FUNC_PURE,      fn_min },
    { "max",     3, 2, FUNC_PURE,      fn_max },
    { "abs",     3, 1, FUNC_PURE,      fn_abs },
    { "sign",    4, 1, FUNC_PURE,      fn_sign },
    { "timeout", 7, 1, FUNC_RAW_INDEX, (csp_func_fn)fn_timeout_raw },
    { "print",   5, 1, FUNC_RAW_INDEX|FUNC_IMMEDIATE, (csp_func_fn)fn_print_raw },
    { "tick",    4, 0, FUNC_PURE,      fn_tick },
    { "cycle",   5, 0, FUNC_PURE,      fn_cycle },

    { "pln0",     4, 0, FUNC_PURE,      fn_pln },
    { "pln1",     4, 1, FUNC_PURE,      fn_pln },
    { "pln2",     4, 2, FUNC_PURE,      fn_pln },
    { "pln3",     4, 3, FUNC_PURE,      fn_pln },
    { "pln4",     4, 4, FUNC_PURE,      fn_pln },    
};

const uint8_t csp_num_builtin_funcs = sizeof(csp_builtin_funcs)/sizeof(csp_builtin_funcs[0]);

// Lookup function by name - returns builtin index (positive) or user index (negative-1)
// Returns 0 if not found
int csp_lookup_func(csp_rt_t* st, const char* name, uint8_t namelen)
{
    int i;
    // Check user functions first
    if (st->user_funcs) {
	for (i = 0; i < st->num_user_funcs; i++) {
	    if (st->user_funcs[i].namelen == namelen &&
		memcmp(st->user_funcs[i].name, name, namelen) == 0) {
		return -(i + 1);  // negative = user function
	    }
	}
    }
    // Check builtin functions
    for (i = 1; i < csp_num_builtin_funcs; i++) {
	if (csp_builtin_funcs[i].namelen == namelen &&
	    memcmp(csp_builtin_funcs[i].name, name, namelen) == 0) {
	    return i;  // positive = builtin function
	}
    }
    return 0;  // not found
}

const char* csp_op_name(opcode_t op)
{
    if (op == OP_CALL)
	return "call";
    tok_t tok = csp_opcode_to_tok(op);
    if (tok < 0)
	return "?";
    return op_table[tok].name;
}

// fixme: table
tok_t csp_opcode_to_tok(opcode_t opcode)
{
    int i = 0;
    while(op_table[i].tok != LAST) {
	if ((op_table[i].ttype == TOKT_INSTR) &&
	    (op_table[i].code == opcode))
	    return op_table[i].tok;
	i++;
    }
    return -1;
}


int find_op(char* name, int name_len)
{
    int i = 0;
    while(op_table[i].tok != LAST) {
	if ((op_table[i].name_len == name_len) &&
	    (memcmp(op_table[i].name, name, name_len) == 0))
	    return i;
	i++;
    }
    return -1;
}

static inline int arity(tok_t op)
{
    return op_table[op].arity;
}

static inline int prec(tok_t op)
{
    return op_table[op].prec;
}

static inline int assoc(tok_t op)
{
    return op_table[op].assoc;
}

// Function calls are stored in ostack as (LAST + 1 + func_index)
// func_index encodes: (index << 1) | is_user
// Note: must use LAST (not LAST_NODE) to avoid overlap with LP, RP, etc.
#define FUNC_MARKER_BASE (LAST + 1)
#define IS_FUNC_MARKER(op) ((op) >= FUNC_MARKER_BASE)
#define MAKE_FUNC_MARKER(idx, is_user) (FUNC_MARKER_BASE + ((idx) << 1) + (is_user))
#define FUNC_MARKER_INDEX(op) (((op) - FUNC_MARKER_BASE) >> 1)
#define FUNC_MARKER_IS_USER(op) (((op) - FUNC_MARKER_BASE) & 1)

#define op_ADD(y, z)  ((y)+(z))
#define op_SUB(y, z)  ((y)-(z))
#define op_MUL(y, z)  ((y)*(z))
#define op_DIV(y, z)  ((y)/(z))
#define op_REM(y, z)  ((y)%(z))
#define op_AND(y, z)  ((y)&(z))
#define op_OR(y, z)   ((y)|(z))
#define op_XOR(y, z)  ((y)^(z))
// logical 1 == -1 (all bits set)
#define op_ANDAND(y, z) (-((y)&&(z)))
#define op_OROR(y, z) (-((y)||(z)))
#define op_LT(y, z)   (-((y)<(z)))
#define op_LTE(y, z)  (-((y)<=(z)))
#define op_GT(y, z)   (-((y)>(z)))
#define op_GTE(y, z)  (-((y)>=(z)))
#define op_EQEQ(y, z) (-((y)==(z)))
#define op_NEQ(y, z)  ((y)!=(z))
#define op_SLA(y, z)  ((y) << (z))
#define op_SRA(y, z)  ((y) >> (z))
#define op_NOT2(y,z)  (~BOOL((y)))
#define op_NEG2(y,z)  (-(y))
#define op_POS2(y,z)  (y)
#define op_INV2(y,z)  (~(y))
#define op_COMMA(y,z) z

#define op_NOT(y)     (~BOOL((y)))
#define op_NEG(y)     (-(y))
#define op_POS(y)     (y)
#define op_INV(y)     (~(y))

#define MAKE_OP2(name)						\
    static ivalue_t CAT2(f_,name)(ivalue_t x, ivalue_t y)	\
    {								\
	return CAT2(op_,name)(x, y);				\
    }

MAKE_OP2(ADD);
MAKE_OP2(SUB);
MAKE_OP2(MUL);
MAKE_OP2(DIV);
MAKE_OP2(REM);
MAKE_OP2(AND);
MAKE_OP2(OR);
MAKE_OP2(XOR);
MAKE_OP2(ANDAND);
MAKE_OP2(OROR);
MAKE_OP2(LT);
MAKE_OP2(LTE);
MAKE_OP2(GT);
MAKE_OP2(GTE);
MAKE_OP2(EQEQ);
MAKE_OP2(NEQ);
MAKE_OP2(SLA);
MAKE_OP2(SRA);
MAKE_OP2(INV2);
MAKE_OP2(NEG2);
MAKE_OP2(POS2);
MAKE_OP2(NOT2);
MAKE_OP2(COMMA);

static ivalue_t (*eval_tab[])(ivalue_t y, ivalue_t z) =
{
    [OP_ADD] = f_ADD,
    [OP_SUB] = f_SUB,
    [OP_MUL] = f_MUL,
    [OP_DIV] = f_DIV,
    [OP_REM] = f_REM,
    [OP_SLA] = f_SLA,
    [OP_SRA] = f_SRA,    
    [OP_AND] = f_AND,
    [OP_OR] = f_OR,
    [OP_XOR] = f_XOR,
    [OP_ANDAND] = f_ANDAND,
    [OP_OROR] = f_OROR,    
    [OP_LT] = f_LT,
    [OP_LTE] = f_LTE,
    [OP_GT] = f_GT,
    [OP_GTE] = f_GTE,
    [OP_EQEQ] = f_EQEQ,
    [OP_NEQ] = f_NEQ,
    // unary versions (treated as binary with z ignored)
    [OP_INV] = f_INV2,
    [OP_NEG] = f_NEG2,
    [OP_POS] = f_POS2,
    [OP_NOT] = f_NOT2,
    // other
    [OP_COMMA] = f_COMMA,
};

int csp_print_value(csp_rt_t* st, vtype_t vt, value_t val)
{
    switch(vt) {
    case V_INTEGER: return csp_print_int(val.i);
    case V_UNSIGNED: return csp_print_uint(val.u);
    case V_FLOAT: return csp_print_float(val.f);
    case V_STRING: return csp_print_str(&st->str[val.s]);
    default: return csp_print_str("???");
    }
}

static int unpack_args(csp_rt_t* st, index_t arg, index_t* argv, int max_args)
{
    int i = 0;
    while (IS_INSTR(arg) && (st->instr[INDEX(arg)].op == OP_COMMA)) {
	if (i >= max_args)
	    return -1;
	argv[i++] = st->instr[INDEX(arg)].y;
	arg = st->instr[INDEX(arg)].z;
    }
    argv[i++] = arg;
    return i;
}

static int eval_args(csp_rt_t* st, index_t arg, value_t* argv, int max_args)
{
    int i = 0;
    while (IS_INSTR(arg) && (st->instr[INDEX(arg)].op == OP_COMMA)) {
	printf("COMMA\n");
	if (i >= max_args)
	    return -1;
	argv[i++] = csp_value(st, st->instr[INDEX(arg)].y);
	arg = st->instr[INDEX(arg)].z;
    }
    argv[i++] = csp_value(st, arg);
    return i;
}


int csp_eval0(csp_rt_t* st, int n)
{
    index_t nx = MAKE_INDEX(0, n, TAG_INSTR);
    opcode_t op = st->instr[n].op;

#if defined(WANT_STATISTICS) && (WANT_STATISTICS==1)
    st->num_eval0++;
#endif
    // fprintf(stdout, "csp_eval0: instr = %d\n", n);
    
    switch(op) {
    case OP_RULE: {
	int n1;
	if (csp_ivalue(st,st->instr[n].z))
	    n1 = n+1;
	else
	    n1 = INDEX(st->instr[n].y)+1;
#if defined(WANT_REACTIVE) && (WANT_REACTIVE==1)
	if (st->reactive) {
	    // fixme: mod?
	    csp_enq(st, MAKE_INDEX(0,n1,TAG_INSTR));
	    return n1;
	}
#endif
	// fprintf(stdout, "jump %d\n", n1);
	return n1;
    }
    case OP_ENTER: // skip y + 2 
	return n + st->instr[n].y + 2;
    case OP_NEW:
	if (!st->reactive) {
	    index_t ent = st->instr[n].y;
	    index_t mod = st->instr[n].z;
	    // in non-reactive mode this is like a call
	    st->stack[st->esp].ix = n+1;
	    st->stack[st->esp].so = st->so;
	    st->stack[st->esp].iq = st->iq;
	    st->so = st->instr[n].z;
	    st->esp++;
	    st->iq = st->decl[INDEX(mod)].mq.iq;
	    return INDEX(ent)+1; // first instruction
	}
	return n+1;
    case OP_LEAVE: // should not happen?
	if (!st->reactive) {
	    // return in non-reactive-mode
	    if (st->esp == 0)
		return st->nn; // make it stop
	    st->esp--;
	    st->so = st->stack[st->esp].so;
	    n = st->stack[st->esp].ix;
	    return n;
	}
	return n+1;
    case OP_EQ: { // plain assign
	value_t zv = csp_value(st, st->instr[n].z);
	csp_set_value(st, st->instr[n].y, zv);
	csp_set_value(st, nx, zv);
	// fprintf(stdout, "%d = v%d = %d (%d)\n",
	// zv.i, INDEX(st->instr[n].y), zv.i,
	// csp_ivalue(st, nx));
	return n+1;
    }
    case OP_CALL: {
	// y: function index (low bit: 0=builtin, 1=user), index >> 1
	// z: argument (0/1 arg) or OP_COMMA instruction (2+ args)
	index_t func_id = st->instr[n].y;
	const csp_func_t* func;
	value_t args[MAX_ARGS];

	// Get function pointer
	if ((func_id & 1) == 0) {
	    uint8_t bi = func_id >> 1;
	    if (bi < csp_num_builtin_funcs)
		func = &csp_builtin_funcs[bi];
	    else
		func = NULL;
	}
	else {
	    uint8_t ui = func_id >> 1;
	    if (st->user_funcs && ui < st->num_user_funcs)
		func = &st->user_funcs[ui];
	    else
		func = NULL;
	}

	if (func && func->fn) {
	    ivalue_t ixv;	    
	    if (func->flags & FUNC_RAW_INDEX) {
		// RAW_INDEX: pass instruction pointer directly
		csp_func_raw_fn raw_fn = (csp_func_raw_fn)func->fn;
		ixv = raw_fn(st, &st->instr[n]);
	    }
	    else {
		// assert(func->nargs <= MAX_ARGS);
		if (func->nargs > 0)
		    eval_args(st, st->instr[n].z, args, func->nargs);
		ixv = func->fn(st, args, func->nargs);
	    }
	    csp_set_ivalue(st, nx, ixv);
	}
	return n+1;	
    }
    default:
	if ((op >= 0) && (op < OP_LAST)) {
	    ivalue_t iyv = csp_ivalue(st, st->instr[n].y);
	    ivalue_t izv = csp_ivalue(st, st->instr[n].z);
	    ivalue_t ixv = eval_tab[op](iyv, izv);
	    csp_set_ivalue(st,nx,ixv);
	    // fprintf(stdout, "%d = %d %s %d (%d)\n",
	    // ixv, iyv, csp_op_name(op), izv,
	    // csp_ivalue(st, nx));
	}
	return n+1;	
    }
}

// undo all values
void csp_undo(csp_rt_t* st)
{
#if defined(WANT_TRANSACTION) && (WANT_TRANSACTION==1)
    int i;
    if (st->transaction) {
	for (i = st->up; i >= 0; i--) {
	    index_t x = st->undo[i].x;
	    int j = st_index(st, x);
	    if (IS_INSTR(x)) {
		bitset_clr(st->xset,x);
		st->xval[j] = st->undo[i].v;
	    }
	    else {
		bitset_clr(st->dset,x);
		st->dval[j] = st->undo[i].v;
	    }
	}
	st->anyx = CSP_FALSE;
	st->anyd = CSP_FALSE;
	st->up = 0;
    }
#endif
}

void csp_commit(csp_rt_t* st)
{
#if defined(WANT_TRANSACTION) && (WANT_TRANSACTION==1)        
    if (st->transaction) {
	st->up = 0; // reset
    }
#endif
    bitset_zero(st->dset);
    bitset_zero(st->xset);
    st->anyx = CSP_FALSE;
    st->anyd = CSP_FALSE;   
}

// run eval0 on all nodes
// FIXME: skip module defs
//        run  module code when mod is found
//             return on end marker

index_t csp_eval(csp_rt_t* st)
{
    index_t n = 0;
    index_t x = BAD_INDEX;

    st->cycle++;
    // fprintf(stdout, "csp_eval: cycle = %d\n", st->cycle);
    
    while(n < st->nn) {
	n = csp_eval0(st, n);
	x = n;
    }
    return x;
}

// run queue until empty
index_t csp_react(csp_rt_t* st)
{
    st->cycle++;
#if defined(WANT_REACTIVE) && (WANT_REACTIVE==1)    
    index_t x0, x1 = BAD_INDEX;
    // fixme: eval node once! optional?
    if (st->reactive) {
	while((x0 = csp_deq(st)) != BAD_INDEX) {
	    csp_eval0(st, x0);
	    x1 = x0;
	}
    }
    return x1;
#else
    // fixme: probably need error log...
    return BAD_INDEX;
#endif
}

// look for symbol among nodes in range [start, stop)
index_t lookup_decl_in(csp_rt_t* st, char* name, int name_len,
		       int start, int stop)
{
    index_t i = start;
    
    while(i < stop) {
	int pos = st->decl[i].name;
	int len = st->str[pos-1];
	if ((len == name_len) &&
	    (memcmp(decl_name(st, MAKE_INDEX(0,i,TAG_DECL)),
		    name, name_len)==0)) {
	    return MAKE_INDEX(0, i, TAG_DECL);
	}
	if (st->decl[i].type == DECL_MODULE) // skip module def
	    i += (st->decl[i].md.n+1); // skip elements and END
	i++;
    }
    return BAD_INDEX;    
}

index_t lookup_decl(csp_rt_t* st, char* name, int name_len)
{
    return lookup_decl_in(st, name, name_len, INDEX(st->mdef)+1, st->nd);
}

index_t csp_lookup_decl(csp_rt_t* st, char* module, char* name)
{
    if (module == NULL) {
	return lookup_decl(st, name, strlen(name));
    }
    else {
	index_t mx = lookup_decl(st, module, strlen(module));
	ivalue_t n;
	if (mx == BAD_INDEX)
	    return BAD_INDEX;
	n = st->decl[mx].md.n;  // number of elements
	return lookup_decl_in(st,name,strlen(name),
			      INDEX(mx)+1,INDEX(mx)+1+n);
    }
}

index_t lookup_const(csp_rt_t* st, vtype_t vt, value_t v)
{
    index_t i;
    for (i = 0; i < st->nd; i++) {
	if (IS_CONST(st, i) && (vt == st->decl[i].vt)) {
	    if (st->decl[i].cn.init.u == v.u)  // binary compare!
		return MAKE_INDEX(0,i,TAG_DECL);
	}
    }
    return BAD_INDEX;
}

index_t lookup_string_const(csp_rt_t* st, char* str, int len)
{
    index_t i;
    for (i = 0; i < st->nd; i++) {
	if (IS_CONST(st, i) && (st->decl[i].vt == V_STRING)) {
	    sindex_t si = st->decl[i].cn.init.s;
	    int sn = st->str[si-1];  // length is in byte before spos
	    if ((sn == len) &&
		(strcmp(str, &st->str[st->decl[i].cn.init.s]) == 0))
		return MAKE_INDEX(0,i,TAG_DECL);		
	}
    }
    return BAD_INDEX;
}

// each string is installed like
//  [3] 'a' 'b' 'c' '\0'
// length byte characters terminated with 0
// position return is pos efter length byte
int new_string(csp_rt_t* st, char* name, int len)
{
    sindex_t pos = st->strp - (len+2);
    if (pos <= 0)
	return -1;
    st->strp = pos;  // allocate
    st->str[pos] = len;
    memcpy(&st->str[pos+1],name,len);
    st->str[pos+1+len] = '\0';
    return pos+1;
}

index_t next_decl_index(csp_rt_t* st)
{
    index_t ix;
    if (st->nd >= MAX_DECLS)
	return BAD_INDEX;
    ix = MAKE_INDEX(0, st->nd, TAG_DECL);
    st->nd++;
    return ix;
}

index_t next_instr_index(csp_rt_t* st)
{
    index_t ix;
    if (st->nn >= MAX_INSTRS)
	return BAD_INDEX;
    ix = MAKE_INDEX(0, st->nn, TAG_INSTR);
    st->nn++;
    return ix;
}

// install a new decl

index_t csp_new_decl(csp_rt_t* st, char* name, int name_len, decl_t type)
{
    index_t i;
    int pos;
    
    if ((i = next_decl_index(st)) == BAD_INDEX)
	return BAD_INDEX;
    pos = 0;
    if (name != NULL) {
	if ((pos = new_string(st, name, name_len)) < 0)
	    return BAD_INDEX;
    }
    st->decl[INDEX(i)].type = type;    
    st->decl[INDEX(i)].name = pos;
    st->decl[INDEX(i)].vt = V_INTEGER;
    return i;
}

index_t new_signed_const(csp_rt_t* st, ivalue_t v)
{
    index_t ix;
    int i;
    if ((ix = csp_new_decl(st,NULL,0,DECL_CONSTANT)) == BAD_INDEX)
	return BAD_INDEX;
    i = INDEX(ix);
    st->decl[i].res = MAKE_RES(8*sizeof(ivalue_t));
    st->decl[i].vt = V_INTEGER;
    st->decl[i].cn.init.i = v;
    st->dval[i].i = v;
    return ix;
}

index_t new_float_const(csp_rt_t* st, fvalue_t v)
{
    index_t ix;
    int i;
    if ((ix = csp_new_decl(st,NULL,0,DECL_CONSTANT)) == BAD_INDEX)
	return BAD_INDEX;
    i = INDEX(ix);
    st->decl[i].res = MAKE_RES(8*sizeof(fvalue_t));
    st->decl[i].vt = V_FLOAT;
    st->decl[i].cn.init.f = v;
    st->dval[i].f = v;
    return ix;
}

index_t new_string_const(csp_rt_t* st, char* str, int len)
{
    index_t ix;
    int pos, i; 
    if ((ix = csp_new_decl(st,NULL,0,DECL_CONSTANT)) == BAD_INDEX)
	return BAD_INDEX;
    if ((pos = new_string(st, str, len)) < 0)
	return BAD_INDEX;
    i = INDEX(ix);
    st->decl[i].res = MAKE_RES(STRING_BITS);
    st->decl[i].vt = V_STRING;
    st->decl[i].cn.init.s = pos;
    st->dval[i].s = pos;    
    return ix;
}

index_t csp_new_node(csp_rt_t* st, opcode_t op, index_t y, index_t z)
{
    index_t x;
    int i;
    if ((x = next_instr_index(st)) == BAD_INDEX)
	return BAD_INDEX;
    i = INDEX(x);
    st->instr[i].op = op;
    st->instr[i].cond = st->cond;
    st->instr[i].y = y;
    st->instr[i].z = z;
    if (st->mdef != 0)
	x = MAKE_INDEX(ANY_MOD, INDEX(x), TAG(x));
    return x;
}

index_t new_expr2(csp_rt_t* st, opcode_t op, index_t y, index_t z)
{
    // relax this?!
    if ((op == OP_EQ) && IS_CONST(st,INDEX(y)))
	return PARSE_ERROR;
    return csp_new_node(st, op, y, z);
}

index_t new_expr1(csp_rt_t* st, opcode_t op, index_t y)
{
    return csp_new_node(st, op, y, ZERO);
}

index_t new_expr0(csp_rt_t* st, opcode_t op)
{
    return csp_new_node(st, op, ZERO, ZERO);
}


// Create OP_CALL instruction
// y = (func_index << 1) | is_user
// z = args_ix (single arg or OP_COMMA chain)
index_t new_func_call(csp_rt_t* st, int func_idx, int is_user, uint8_t nargs, index_t args_ix)
{
    int n;
    index_t argv[MAX_ARGS];
    index_t func_id = (func_idx << 1) | is_user;

    if (nargs > MAX_ARGS)
	return PARSE_ERROR;

    // Validate argument count by unpacking
    n = unpack_args(st, args_ix, argv, MAX_ARGS);
    printf("n = %d, args_ix=%d\n", n, args_ix);
    if ((n < 0) || (n != nargs))
	return PARSE_ERROR;

    // Create OP_CALL: y=func_id, z=args (or OP_COMMA chain)
    return csp_new_node(st, OP_CALL, func_id, args_ix);
}

// call csp_csr to build an reactive graph

// check if x depends on y for it's computation
/*
static inline int depend_on_y(csp_rt_t* st, index_t i)
{
    opcode_t op = st->elem[i].op;
    index_t y;

    if (op == OP_RULE) return 0;     // x: (a=1) ? cond (only depend on z)
    y = st->instr[i].y;
    if (IS_CONST(st,y)) return 0;  // x: const op z (y does never change)
    return 1;
}

// check if x depends on z for it's computation
static inline int depend_on_z(csp_rt_t* st, index_t i)
{
    index_t z = st->instr[i].z;
    if (IS_CONST(st,z)) return 0;
    return 1;
}
*/

#define DEP(st,i,y) ((IS_INSTR((y)) && !IS_COND((st),(y))) || \
		     (IS_DECL((y)) && \
		      (!IS_COND((st),(i)) && !IS_CONST((st),(y)))))
void csp_csr(csp_rt_t* st)
{
#if defined(WANT_REACTIVE) && (WANT_REACTIVE==1)
    int i, x0, x1;
    int in_module = 0;
    index_t wr[MAX_INDEX];

    // setup idg for all nodes
    // x : y op z   count y
    // 
    for (i = 0; i < st->nn; i++) {
	if (IS_INSTR(i)) {
	    index_t y = INDEX(st->instr[i].y);
	    index_t z = INDEX(st->instr[i].z);
	    if (DEP(st,i,y))
		st->idg[y]++;
	    if (DEP(st,i,z))
		st->idg[z]++;
	}
    }

    // calculate leaf offsets
    x0 = 0;
    st->ofs[x0] = 0;
    for (i = 0; i < st->nn; i++) {
	x1 = i+1;
	st->ofs[x1] = st->ofs[x0] + st->idg[x0];
	x0 = x1;
    }
    
    memcpy(wr, st->ofs, MAX_INDEX*sizeof(index_t));
    
    // fill in parents in edg array (fixme maybe moved into loop above)
    for (i = 0; i < st->nn; i++) {
	switch(st->instr[i].op) {
	case OP_ENTER: in_module = 1; break;
	case OP_LEAVE: in_module = 0; break;
	case OP_NEW: break;
	default: {
	    index_t y = st->instr[i].y;
	    index_t z = st->instr[i].z;
	    index_t x = in_module ? MAKE_INDEX(ANY_MOD,i,TAG_INSTR) : i;
	    if (DEP(st,i,y))
		st->edg[wr[INDEX(y)]++] = x;
	    if (DEP(st,i,z))
		st->edg[wr[INDEX(z)]++] = x;
	    break;
	}
	}
    }
#endif
}

#define TOK(t) do { tok = (t); goto done; } while(0)
#define SYM(t,p,l) do { tok = (t); tokv->str=(p); tokv->len=(l); goto done; } while(0)
#define TOK_INT(v) do { tok = INT; tokv->val.i = (v); goto done; } while(0)
#define TOK_FLT(v) do { tok = FLT; tokv->val.f = (v); goto done; } while(0)

static inline int dec(int c)
{
    if ((c >= '0') && (c <= '9'))
	return (c - '0');
    return 0;
}

static inline int hex(int c)
{
    if ((c >= '0') && (c <= '9'))
	return (c - '0');
    if ((c >= 'a') && (c <= 'f'))
	return (c - 'a')+10;
    if ((c >= 'A') && (c <= 'F'))
	return (c - 'A')+10;
    return 0;
}
    

int csp_next_token(char* str, tok_t* tokp, tokval_t* tokv)
{
    char* str0 = str;
    int c;
    tok_t tok;
next:
    c = *str++;
    switch(c) {
    case '\0': str--; TOK(NONE);
    case ' ':
    case '\t': goto next;
    case '\r':
	if (*str == '\n') {
	    str++; TOK(NEWLINE);
	}
	TOK(NEWLINE);
    case '\n': TOK(NEWLINE);
    case ',': TOK(COMMA);
    case '.': TOK(DOT);	
    case ':': TOK(COLON);
    case '#': TOK(HASH);
    case '?': TOK(QUEST);	
    case '*': TOK(ASTERISK);
    case '/':
	if (*str == '/') {
	    str++;
	    while(*str) {
		if (*str == '\n')
		    TOK(NEWLINE);
		if (*str == '\r') {
		    str++;
		    if (*str == '\n') {
			TOK(NEWLINE);
		    }
		    TOK(NEWLINE);
		}
		str++;
	    }
	    TOK(NEWLINE);
	}
	else {
	    TOK(SLASH);
	}
    case '%': TOK(PERCENT);
    case '&':
	switch(*str) {
	case '&': str++; TOK(AMPAMP);
	default: TOK(AMP);
	}
	break;
    case '|':
	switch(*str) {
	case '|': str++; TOK(BARBAR);
	default: TOK(BAR);
	}
	break;
    case '^': TOK(CIRC);
    case '~': TOK(TILDE);
    case '!':
	switch(*str) {
	case '=': str++; TOK(NEQ);
	default: TOK(EXCLAMATION);
	}
	break;
    case '=':
	switch(*str) {
	case '=': str++; TOK(EQEQ);
	default: TOK(EQ);
	}
	break;
    case '<':
	switch(*str) {
	case '=': str++; TOK(LTEQ);
	case '<': str++; TOK(LTLT);
	default: TOK(LT);
	}
	break;
    case '>':
	switch(*str) {
	case '=': str++; TOK(GTEQ);
	case '>': str++; TOK(GTGT);
	default: TOK(GT);
	}
	break;
    case '[': TOK(LB);
    case ']': TOK(RB);	
    case '(': TOK(LP);
    case ')': TOK(RP);
    case '-': TOK(MINUS);
    case '+': TOK(PLUS);
    case '"': {
	char* qstr = str;  // point to first string char
	char* dst = str;
	int c;
	int len = 0;
	while((c = *str++) && (c != '"')) {
	    if (c == '\\') {
		switch((c=*str++)) {
		case 'a': c = '\a'; break;
		case 'b': c = '\b'; break;
		case 'e': c = '\e'; break;	
		case 'f': c = '\f'; break;
		case 'n': c = '\n'; break;
		case 'r': c = '\r'; break;
		case 't': c = '\t'; break;
		case 'v': c = '\v'; break;
		default: break;
		}
	    }
	    *dst++ = c;
	    len++;
	}
	SYM(STR, qstr, len);
    }
    default:
	if ((c == '0') && (*str == 'x')) {
	    ivalue_t v = 0;
	    str++;
	    while(isxdigit(*str)) {
		v = v*16 + hex(*str++);
	    }
	    TOK_INT(v);
	}
	if (isdigit(c)) {
	    ivalue_t v = dec(c);
	    while(isdigit(*str)) {
		v = v*10 + dec(*str++);
	    }
            // parse simple fraction for now	    
	    if ((str[0] == '.') && isdigit(str[1])) {
		float b = 0.1;
		float f = 0.0;
		str++;
		while(isdigit(*str)) {
		    f = f + (b*dec(*str++));
		    b /= 10.0;
		}
		f += v;
		TOK_FLT(f);
	    }
	    TOK_INT(v);
	}
	else if (isalpha(c)) {
	    char *name = str-1;
	    int len = 1;
	    int i;
	    while((isalpha(*str)||isdigit(*str)) &&
		  (len < MAX_NAME_LEN)) {
		str++;
		len++;
	    }
	    if ((i = find_op(name,len)) >= 0)
		TOK(op_table[i].tok);
	    SYM(WORD, name, len);
	}
	else
	    return -1;
	break;
    }
done:
    *tokp = tok;
    return str - str0;
}

// scan one line of tokens
int csp_scan_line(char* str, tok_t* tok, tokval_t* val, size_t* num_toks)
{
    char* str0 = str;
    size_t i;
    size_t max_toks = *num_toks;

    i = 0;
    while(i < max_toks) {
	int n = csp_next_token(str, &tok[i], &val[i]);
	if (n < 0)
	    return -1;
	str += n;
	if ((tok[i] == NEWLINE) || (tok[i] == NONE)) {
	    *num_toks = i;
	    return str-str0;
	}
	i++;
    }
    return -1;
}

// num_toks is number of tokens and value on input
// num_toks is number of tokens consumed on output
index_t csp_parse_expr(csp_rt_t* st, tok_t* tok, tokval_t* val,
		       size_t* num_toks)
{
    tok_t op;
    tokval_t tval;    
    tok_t pop = NONE;   // previous operator/token
    int pp = 0;         // operator stack pointer
    int ep = 0;         // expression stack pointer
    tok_t ostack[MAX_PARSE_STACK_DEPTH];
    index_t xstack[MAX_PARSE_STACK_DEPTH];
    index_t ix;
    int i = 0;
    size_t n = *num_toks;
next:
    if ((i >= n) || (tok[i]==NEWLINE) || (tok[i]==NONE))  // end-of-list
	goto out;
    op   = tok[i];
    tval = val[i];
    i++;
    switch(op) {
    case QUEST: i--; goto out;
    case COMMA: goto operator;
    case ASTERISK:  goto operator;
    case SLASH: goto operator;
    case PERCENT: goto operator;
    case AMPAMP: goto operator;
    case AMP:   goto operator;	
    case BARBAR:  goto operator;
    case BAR:    goto operator;
    case CIRC:   goto operator;
    case TILDE:  goto operator;
    case PLUS:
	if ((pop == NONE) || (pop == RP) ||
	    ((pop != INT) && (pop != WORD)))
	    op = PLUS1;
	goto operator;	
    case MINUS:
	if ((pop == NONE) || (pop == RP) ||
	    ((pop != INT) && (pop != WORD)))
	    op = MINUS1;
	goto operator;
    case NEQ: goto operator;
    case EXCLAMATION: goto operator;
    case EQEQ: goto operator;
    case EQ: goto operator;
    case LTEQ: goto operator;
    case LTLT: goto operator;
    case LT: goto operator;	
    case GTEQ:goto operator;
    case GTGT:goto operator;
    case GT:goto operator;
    case LP:
	ostack[pp++] = LP; pop = LP; break;
    case RP:
	if (pp == 0)
	    return PARSE_ERROR;
	// Process operators until we hit LP or a function marker
	while(pp && ((op = ostack[pp-1]) != LP) && !IS_FUNC_MARKER(op)) {
	    switch(arity(op)) {
	    case 2:
		ix = new_expr2(st,op_table[op].code,
			       xstack[ep-2],xstack[ep-1]);
		if (ix == BAD_INDEX) return PARSE_ERROR;
		xstack[ep-2] = ix;
		ep--;
		break;
	    case 1:
		ix = new_expr1(st,op_table[op].code,xstack[ep-1]);
		if (ix == BAD_INDEX) return PARSE_ERROR;
		xstack[ep-1] = ix;
		break;
	    case 0:
		return PARSE_ERROR;
	    }
	    pp--;
	}
	if (pp && IS_FUNC_MARKER(ostack[pp-1])) {
	    // Function call - get func info and generate OP_CALL
	    // Note: LP was skipped in WORD case, so no LP to pop
	    tok_t marker = ostack[--pp];
	    int func_idx = FUNC_MARKER_INDEX(marker);
	    int is_user = FUNC_MARKER_IS_USER(marker);
	    index_t args_ix;
	    const csp_func_t* func = NULL;

	    if (is_user) {
		if (st->user_funcs && func_idx < st->num_user_funcs)
		    func = &st->user_funcs[func_idx];
	    } else {
		if (func_idx < csp_num_builtin_funcs)
		    func = &csp_builtin_funcs[func_idx];
	    }
	    if (!func || !func->fn)
		return PARSE_ERROR;
	    // args are on xstack, combined with OP_COMMA if multiple
	    args_ix = (ep > 0) ? xstack[ep-1] : ZERO;
	    ix = new_func_call(st, func_idx, is_user, func->nargs, args_ix);
	    if (ix == BAD_INDEX) return PARSE_ERROR;
	    if (func->nargs > 0)
		xstack[ep-1] = ix;
	    else
		xstack[ep++] = ix;
	}
	else if (pp && ostack[pp-1] == LP) {
	    pp--;  // pop the LP for regular parentheses
	}
	else {
	    return PARSE_ERROR;  // mismatched )
	}
	op = RP;
	pop = INT;
	break;
    case INT:
	if ((ix = lookup_const(st, V_INTEGER, tval.val)) == BAD_INDEX)
	    ix = new_signed_const(st,tval.val.i);
	if (ix == BAD_INDEX) return PARSE_ERROR;
	xstack[ep++] = ix;
	pop = INT;
	break;
    case FLT:
	if ((ix = lookup_const(st, V_FLOAT, tval.val)) == BAD_INDEX)
	    ix = new_float_const(st,tval.val.f);
	if (ix == BAD_INDEX) return PARSE_ERROR;
	xstack[ep++] = ix;
	pop = FLT;
	break;
    case STR:
	if ((ix = lookup_string_const(st,tval.str,tval.len)) == BAD_INDEX)
	    ix = new_string_const(st,tval.str,tval.len);
	if (ix == BAD_INDEX) return PARSE_ERROR;
	xstack[ep++] = ix;
	pop = STR;
	break;
    case WORD: {
	// First check if this is a function call (WORD followed by LP)
	int func_res = csp_lookup_func(st, tval.str, tval.len);
	if (func_res != 0 && tok[i] == LP) {
	    // It's a function call - push marker to ostack and skip LP
	    int is_user = (func_res < 0) ? 1 : 0;
	    int func_idx = is_user ? (-func_res - 1) : func_res;
	    ostack[pp++] = MAKE_FUNC_MARKER(func_idx, is_user);
	    i++;  // skip the LP token
	    pop = LP;
	}
	else {
	    // Not a function - regular variable/decl lookup
	    if ((ix = lookup_decl(st,tval.str,tval.len)) == BAD_INDEX) {
		if ((ix = csp_new_decl(st,tval.str,tval.len,DECL_VARIABLE)) == BAD_INDEX)
		    return PARSE_ERROR;
		st->decl[INDEX(ix)].va.init.i = 0;
	    }
	    else if ((st->decl[INDEX(ix)].type == DECL_MOD) &&
		     (tok[i] == DOT) && (tok[i+1] == WORD)) {
		index_t mx = st->decl[INDEX(ix)].mq.mx; // module def
		ivalue_t vn = st->decl[INDEX(mx)].md.n;  // number of elements
		index_t jx;

		tval = val[i+1];
		if ((jx = lookup_decl_in(st,tval.str,tval.len,
					 INDEX(mx)+1,INDEX(mx)+1+vn)) == BAD_INDEX)
		    return PARSE_ERROR;
		ix = MAKE_INDEX(st->decl[INDEX(ix)].mq.iq,jx,TAG_DECL);
		i += 2;
	    }
	    if (st->mdef != 0)
		ix = MAKE_INDEX(ANY_MOD, INDEX(ix), TAG(ix));
	    xstack[ep++] = ix;
	    pop = WORD;
	}
	break;
    }
    default:
	return PARSE_ERROR;
    }
    goto next;
operator:
    {
	int p1;
	if ((p1 = prec(op)) == -1)
	    return PARSE_ERROR;
	if (pp == 0) {
	    ostack[pp++] = op;
	}
	else {
	    tok_t op2 = ostack[pp-1];
	    int p2 = prec(op2);
	    
	    if ( ((p2 > p1) && (op2 != LP)) ||
		 ((p2 == p1) && (assoc(op2) < 0))) {
		switch(arity(op2)) {
		case 2:
		    ix = new_expr2(st,op_table[op2].code,xstack[ep-2],
				   xstack[ep-1]);
		    if (ix == BAD_INDEX) return PARSE_ERROR;
		    xstack[ep-2] = ix;
		    ep--;
		    break;
		case 1:
		    ix = new_expr1(st,op_table[op2].code,xstack[ep-1]);
		    if (ix == BAD_INDEX) return PARSE_ERROR;
		    xstack[ep-1] = ix;
		    break;
		case 0:
		    return PARSE_ERROR;
		}
		pp--;
	    }
	    ostack[pp++] = op;
	}
	pop = op;
    }
    goto next;
out: // expression is terminated with non-expression char
    while(pp > 0) {
	op = ostack[--pp];
	if (op == LP)
	    return PARSE_ERROR;
	switch(arity(op)) {
	case 2:
	    ix = new_expr2(st,op_table[op].code,xstack[ep-2],xstack[ep-1]);
	    if (ix == BAD_INDEX) return PARSE_ERROR;
	    xstack[ep-2] = ix;
	    ep--;
	    break;
	case 1:
	    ix = new_expr1(st,op_table[op].code,xstack[ep-1]);
	    if (ix == BAD_INDEX) return PARSE_ERROR;	    
	    xstack[ep-1] = ix;
	    break;
	case 0:
	    return PARSE_ERROR;
	}
    }
    if (pp < 0)
	return PARSE_ERROR;
    if (ep == 1) {
	*num_toks = i;
	return xstack[0];
    }
    return PARSE_ERROR;    
}

// parse constant
static int parse_value(vtype_t vt, tok_t tok, value_t val, value_t* dst)
{
    value_t r;
    
    if (vt == V_FLOAT) {
	if (tok == FLT)
	    r.f = val.f;
	else if (tok == INT)
	    r.f = (float) val.i;
	else
	    return -1;
    }
    else {
	if (tok == FLT)
	    r.i = (ivalue_t)val.f;
	else if (tok == INT)
	    r.i = val.i;
	else
	    return -1;
    }
    *dst = r;
    return 0;
}

static int expect(tok_t* tok, int i, ...)
{
    va_list ap;
    tok_t t;
    
    va_start(ap, i);
    do {
	t = va_arg(ap, tok_t);
	if ((t != LAST) && (t != tok[i]))
	    return 0;
	i++;
    } while(t != LAST);
    va_end(ap);
    return 1;
}

// '#' 'module' <name>
int csp_parse_module(csp_rt_t* st, tok_t* tok, tokval_t* val, size_t n)
{
    index_t ix;
    index_t jx;

    if (!expect(tok, 0, HASH, MODULE, WORD, LAST))
	return -1;
    if ((ix = lookup_decl(st, val[2].str, val[2].len)) != BAD_INDEX)
	return -1; // already defined
    ix = csp_new_decl(st, val[2].str, val[2].len, DECL_MODULE);
    if (ix == BAD_INDEX) return -1;
    st->mdef = ix;
    if ((jx = csp_new_node(st, OP_ENTER, ZERO, ix)) == BAD_INDEX)
	return -1;
    st->ent = jx;
    st->decl[INDEX(ix)].md.n = 0;
    st->decl[INDEX(ix)].md.ent = st->ent;
    return 0;
}

// '#' 'end' [....]
int csp_parse_end(csp_rt_t* st, tok_t* tok, tokval_t* val, size_t n)
{
    index_t mx, ex, lx;

    if (!expect(tok, 0, HASH, END, LAST))
	return -1;
    if ((mx = st->mdef)) { // stack?
	if ((ex = csp_new_decl(st, NULL, 0, DECL_END)) == BAD_INDEX)
	    return -1;
	st->decl[INDEX(mx)].md.n = (INDEX(ex) - INDEX(mx)) - 1;
	if (st->nm >= MAX_MODULES)
	    return -1;
	if ((lx = csp_new_node(st, OP_LEAVE, ZERO, ZERO)) == BAD_INDEX)
	    return -1;
	st->instr[INDEX(st->ent)].y = (INDEX(lx) - INDEX(st->ent) - 1);
	st->instr[INDEX(lx)].y = st->instr[INDEX(st->ent)].y;
	st->instr[INDEX(lx)].z = st->instr[INDEX(st->ent)].z;
	
	st->module[st->nm++] = mx;
	// stack?
	st->mdef = 0;
	st->ent = 0;
	return 0;
    }
    return -1;
}

// '#' 'variable' <name>[':' <size>] [<opt>+] ['=' <num>]
// <opt> := 'in'|'out'|'inout'|  -- when use as argument in module
//          'signed'|'unsigned'|'float'
int csp_parse_variable(csp_rt_t* st, tok_t* tok, tokval_t* val, size_t n)
{
    ivalue_t res = MAKE_RES(8*sizeof(ivalue_t));
    ivalue_t in=0, out=0;
    vtype_t vt = V_INTEGER;
    value_t def;
    index_t ix;    
    int i;

    if (!expect(tok, 0, HASH, VARIABLE, WORD, LAST))
	return -1;
    i=3;
    if ((tok[i] == COLON) && (tok[i+1] == INT)) {
	res = MAKE_RES(val[i+1].val.i);
	i += 2;
    }
    vt = V_INTEGER;
    def.i = 0;    
opts:
    if (i < n) {
	switch(tok[i]) {
	case UNSIGNED: vt=V_UNSIGNED; def.u = 0; i++; goto opts;
	case INTEGER: vt=V_INTEGER; def.i = 0; i++; goto opts;
	case FLOAT: vt=V_FLOAT; def.f = 0.0; i++;  goto opts;
	case IN: in = 1; i++; goto opts;
	case OUT: out = 1; i++; goto opts;
	case INOUT: in=out=1; i++; goto opts;
	default: break;
	}
    }
    if (tok[i] == EQ) {
	if (parse_value(vt, tok[i+1], val[i+1].val, &def) < 0)
	    return -1;
	i += 2;
    }
    if ((ix = lookup_decl(st, val[2].str, val[2].len)) == BAD_INDEX)
	ix = csp_new_decl(st, val[2].str, val[2].len, DECL_VARIABLE);
    if (ix == BAD_INDEX) return -1;
    i = INDEX(ix);
    st->decl[i].vt = vt;
    st->decl[i].res = res;
    st->decl[i].in = in;
    st->decl[i].out = out;    
    st->decl[i].va.init = def;
    return 0;
}

// '#' 'constant' <name>[':' <size>] [<opt>+] '=' <num>
int csp_parse_constant(csp_rt_t* st, tok_t* tok, tokval_t* val, size_t n)
{
    ivalue_t res;
    value_t cnst;
    vtype_t vt;
    index_t ix;  
    int i;

    // defaults
    vt = V_INTEGER;
    cnst.i = 0;
    res = MAKE_RES(8*sizeof(ivalue_t));

    if (!expect(tok, 0, HASH, CONSTANT, WORD, LAST))
	return -1;
    i=3;
    if ((tok[i] == COLON) && (tok[i+1] == INT)) {
	res = MAKE_RES(val[i+1].val.i);
	i += 2;
    }
    switch(tok[i]) {
    case UNSIGNED: vt=V_UNSIGNED; cnst.u=0; i++; break;
    case INTEGER: vt=V_INTEGER; cnst.i=0; i++;  break;	    	    
    case FLOAT: vt=V_FLOAT; cnst.f=0.0; i++; break;
    default: break;
    }
    if (tok[i] != EQ)
	return -1;
    if (parse_value(vt, tok[i+1], val[i+1].val, &cnst) < 0)
	return -1;	
    i += 2;
    if ((ix = lookup_decl(st, val[2].str, val[2].len)) == BAD_INDEX)
	ix = csp_new_decl(st, val[2].str, val[2].len, DECL_CONSTANT);
    if (ix == BAD_INDEX) return -1;
    i = INDEX(ix);
    st->decl[i].res = res;
    st->decl[i].vt = vt;
    st->decl[i].cn.init = cnst;
    return 0;    
}

// '#' 'digital' <name> [<iodir>|<pull>] [<port>':']<pin>
int csp_parse_digital(csp_rt_t* st, tok_t* tok, tokval_t* val, size_t n)
{
    ivalue_t res = MAKE_RES(1);
    ivalue_t in=0, out=0;
    ivalue_t pu=0, pd=0;
    ivalue_t port=0, pin=0;
    index_t ix;
    int i;

    if (!expect(tok, 0, HASH, DIGITAL, WORD, LAST))
	return -1;
    i = 3;
opts:
    if (i < n) {
	switch(tok[i]) {
	case IN: in = 1; i++; goto opts;
	case OUT: out = 1; i++; goto opts;
	case INOUT: in=out=1; i++; goto opts;
	case PULLUP: pd=0; pu=1; i++; goto opts;
	case PULLDOWN: pu=0; pd=1; i++; goto opts;
	default: break;
	}
    }
    if (expect(tok, i, INT, COLON, INT, LAST)) {
	port = val[i].val.i;
	pin  = val[i+2].val.i;
	i += 3;
    }
    else if (tok[i]==INT) {
	pin = val[i].val.i;
	i++;
    }
    else
	return -1;
    if (!in && !out) in=1;
    if ((ix = lookup_decl(st, val[2].str, val[2].len)) == BAD_INDEX)
	ix = csp_new_decl(st, val[2].str, val[2].len, DECL_DIGITAL);
    if (ix == BAD_INDEX) return -1;
    if (in) {
	if (st->ni >= MAX_INPUTS) return -1;	
	st->input[st->ni++] = ix;
    }
    if (out) {
	if (st->no >= MAX_OUTPUTS) return -1;		
	st->output[st->no++] = ix;
    }
    i = INDEX(ix);
    st->decl[i].res = res;
    st->decl[i].di.pin = pin;
    st->decl[i].di.port = port;
    st->decl[i].in = in;
    st->decl[i].out = out;
    st->decl[i].di.pullup = pu;
    st->decl[i].di.pulldown = pd;
    return 0;
}

//'#' 'analog' <name> [':'<size>] [<opt>*]  [<port>':'] <pin>
//   <opt> := 'in' | 'out' | 'inout' | 'pwm' | 'float' | 'signed' | 'unsigned'
int csp_parse_analog(csp_rt_t* st, tok_t* tok, tokval_t* val, size_t n)
{
    ivalue_t res;
    ivalue_t in=0, out=0;
    ivalue_t port=0, pin=0, pwm=0;
    vtype_t vt;
    index_t ix;
    int i;

    res = MAKE_RES(10);
    vt = V_INTEGER;    

    if (!expect(tok, 0, HASH, ANALOG, WORD, LAST))
	return -1;
    i = 3;
    if ((tok[i]==COLON) && (tok[i+1]==INT)) {
	res = MAKE_RES(val[i+1].val.i);
	i += 2;
    }
opts:
    if (i < n) {
	switch(tok[i]) {
	case UNSIGNED: vt=V_UNSIGNED; i++; goto opts;
	case INTEGER: vt=V_INTEGER; i++; goto opts;
	case FLOAT: vt=V_FLOAT; i++;  goto opts;
	case PWM: pwm = 1; i++; goto opts;
	case IN: in = 1; i++; goto opts;
	case OUT: out = 1; i++; goto opts;
	case INOUT: in=out=1; i++; goto opts;
	default: break;
	}
    }
    if (expect(tok, i, INT, COLON, INT, LAST)) {
	port = val[i].val.i;
	pin  = val[i+2].val.i;
	i += 3;
    }
    else if (tok[i] == INT) {
	pin = val[i].val.i;
	i++;
    }
    else
	return -1;
    if (!in && !out) in=1;
    if ((ix = lookup_decl(st, val[2].str, val[2].len)) == BAD_INDEX)
	ix = csp_new_decl(st, val[2].str, val[2].len, DECL_ANALOG);
    if (ix == BAD_INDEX) return -1;
    if (in) {
	if (st->ni >= MAX_INPUTS) return -1;	
	st->input[st->ni++] = ix;
    }
    if (out) {
	if (st->no >= MAX_OUTPUTS) return -1;		
	st->output[st->no++] = ix;
    }
    i = INDEX(ix);    
    st->decl[i].vt = vt;
    st->decl[i].res = res;
    st->decl[i].in = in;
    st->decl[i].out = out;    
    st->decl[i].an.pin = pin;
    st->decl[i].an.port = port;
    st->decl[i].an.pwm = pwm;
    return 0;            
}

// '#' 'timer' <name> <milliseconds> ['=' '1'|'0']
int csp_parse_timer(csp_rt_t* st, tok_t* tok, tokval_t* val, size_t n)
{
    ivalue_t init = 0;
    index_t ix, px, tx;
    int i;

    if (!expect(tok, 0, HASH, TIMER, WORD, INT, LAST))
	return -1;
    if (st->nt >= MAX_TIMERS) return -1;

    if (tok[4] == EQ) {
	if (tok[5] != INT) return -1;
	init = val[5].val.i;
    }

    if ((px = lookup_const(st, V_INTEGER, val[3].val)) == BAD_INDEX)
	px = new_signed_const(st,val[3].val.i);

    tx = csp_new_decl(st, NULL, 0, DECL_VARIABLE);
    i = INDEX(tx);
    st->decl[i].vt = V_UNSIGNED;
    st->decl[i].res = MAKE_RES(32);
    st->decl[i].va.init.u = 0;
    
    if ((ix = lookup_decl(st, val[2].str, val[2].len)) == BAD_INDEX) {
	if ((ix = csp_new_decl(st, val[2].str, val[2].len, DECL_TIMER)) == BAD_INDEX)
	    return -1;
    }
    st->timer[st->nt++] = ix;
    i = INDEX(ix);
    st->decl[i].tm.running = 0;
    st->decl[i].tm.init = init;
    st->decl[i].tm.px = px;
    st->decl[i].tm.tx = tx;
    return 0;
}

// '#' 'can' <name>[':'<size>] [<opt>*] <can-bit>
// <opt> := 'in' | 'out' | 'inout' | 'float' | 'signed' | 'unsigned'
// 
// <can-bit> :=
//  <frame-id> '[' <bit-pos> ']'
//  <frame-id> '[' <bit-pos> '..' <bit-pos> ']'
//
int csp_parse_can(csp_rt_t* st, tok_t* tok, tokval_t* val, size_t n)
{
    ivalue_t res = MAKE_RES(1);
    ivalue_t in=0, out=0;
    vtype_t vt = V_INTEGER;
    vendian_t endian = E_UNDEFINED;
    index_t ix;
    index_t idx;
    int i;
    int bit0, bit1;

    if (!expect(tok, 0, HASH, CAN, WORD, LAST))
	return -1;
    i=3;
    if ((tok[i] == COLON) && (tok[i+1] == INT)) {
	res = MAKE_RES(val[i+1].val.i);
	i += 2;
    }
opts:
    if (i < n) {
	switch(tok[i]) {
	case UNSIGNED: vt=V_UNSIGNED; i++; goto opts;
	case INTEGER: vt=V_INTEGER; i++; goto opts;
	case FLOAT: vt=V_FLOAT; i++;  goto opts;
	case IN: in = 1; i++; goto opts;
	case OUT: out = 1; i++; goto opts;
	case INOUT: in=out=1; i++; goto opts;
	case LITTLE: endian=E_LITTLE; i++; goto opts;
	case BIG: endian=E_BIG; i++; goto opts;	    	    
	default: break;
	}
    }
    if (!in && !out) in=1;

    if (tok[i] != INT) return -1;  // FIXME allow constant/variable
    if (tok[i+1] != LB) return -1;
    if (tok[i+2] != INT) return -1;
    if (tok[i+3] == RB) {
	bit0 = bit1 = val[i+2].val.i;
    }
    else if (tok[i+3] == DOT)  {
	if (tok[i+4] != DOT) return -1;
	if (tok[i+5] != INT) return -1;
	if (tok[i+6] != RB) return -1;
	bit0 = val[i+2].val.i;
	bit1 = val[i+5].val.i;
    }
    // install fram id
    if ((idx = lookup_const(st, V_INTEGER, val[i].val)) == BAD_INDEX)
	idx = new_signed_const(st,val[i].val.i);
    
    if ((ix = lookup_decl(st, val[2].str, val[2].len)) == BAD_INDEX)
	ix = csp_new_decl(st, val[2].str, val[2].len, DECL_CAN);
    if (ix == BAD_INDEX) return -1;
    if (in) {
	if (st->ni >= MAX_INPUTS) return -1;	
	st->input[st->ni++] = ix;
    }
    if (out) {
	if (st->no >= MAX_OUTPUTS) return -1;		
	st->output[st->no++] = ix;
    }
    i = INDEX(ix);
    st->decl[i].res = res;
    st->decl[i].vt = vt;
    st->decl[i].in = in;
    st->decl[i].out = out;
    st->decl[i].ca.id = idx;
    st->decl[i].ca.bit = bit0;
    st->decl[i].ca.len = (bit1-bit0);
    st->decl[i].ca.endian = endian;
    return 0;
}

// '#' word <name>
int csp_parse_mod(csp_rt_t* st, tok_t* tok, tokval_t* val, size_t n)
{
    index_t mx, ix, jx;
    int i;

    if (!expect(tok, 0, HASH, WORD, WORD, LAST))
	return -1;
    // lookup module
    if ((mx = lookup_decl(st, val[1].str, val[1].len)) == BAD_INDEX) {
	// FIXME: set error reason
	return -1;
    }
    if (st->decl[INDEX(mx)].type != DECL_MODULE) {
	// FIXME: set error reason
	return -1;
    }
    if ((ix = csp_new_decl(st, val[2].str, val[2].len, DECL_MOD)) == BAD_INDEX)
	return -1;
    if ((jx = csp_new_node(st, OP_NEW, st->decl[INDEX(mx)].md.ent, ix)) == BAD_INDEX)
	return -1;
    i = INDEX(ix);
    st->decl[i].mq.mx = mx;
    st->decl[i].mq.iq = st->nq;
    if (st->nq >= MAX_MODS) return -1;
    st->mofs[st->nq] = st->so;
    st->so += st->decl[INDEX(mx)].md.n;
    st->mod[st->nq++] = ix;
    return 0;
}


// <expr> '?' <cond>
// expr = tok[0]...tok[i-1]
// cond = tok[i+1]...tok[num-1]
// first parse condition
// then parse expression

// find index of t among tok or -1 if not found
static int tok_index(tok_t t, tok_t* tok, size_t n)
{
    int i;
    for (i = 0; i < n; i++) {
	if (t == tok[i])
	    return i;
    }
    return -1;
}

//
// Rule nodes:
//  Expr ? Cond
//
//  <Cond>
//  if false goto Else
//  <Expr>
// Else:
//
int csp_parse_rule(csp_rt_t* st, tok_t* tok, tokval_t* val, size_t n)
{
    index_t cx, ex=0, qx;
    size_t num;
    int i;

    if ((i=tok_index(QUEST, tok, n)) < 0)
	return -1;
    // parse condition
    num = n - (i+1);
    if ((cx = csp_parse_expr(st,&tok[i+1],&val[i+1], &num)) == BAD_INDEX)
	return -1;
    if ((qx = new_expr2(st, OP_RULE, CSP_FALSE, cx)) == BAD_INDEX)
	return -1;
    // parse expression after query node and patch in ex
    num = i;
    st->cond = 1;
    ex = csp_parse_expr(st,&tok[0],&val[0],&num);
    st->cond = 0;
    if (ex == BAD_INDEX)
	return -1;
    st->instr[INDEX(qx)].y = ex;
    return 0;
}

index_t lookup_can_range(csp_rt_t* st, index_t idx, ivalue_t p0, ivalue_t p1)
{
    index_t i;
    for (i = 0; i < st->nd; i++) {
	if (IS_CAN(st, i) && (idx == st->decl[i].ca.id)) {
	    if ((st->decl[i].ca.bit == p0) &&
		(st->decl[i].ca.len == MAKE_CAN_LEN((p1-p0)+1)))
		return MAKE_INDEX(0,i,TAG_DECL);
	}
    }
    return BAD_INDEX;
}

index_t make_can_range(csp_rt_t* st, char* str, int len,
		       index_t idx, ivalue_t p0, ivalue_t p1)
{
    index_t ix;
    int i;
    ix = csp_new_decl(st, str, len, DECL_CAN);
    if (ix == BAD_INDEX) return BAD_INDEX;
    i = INDEX(ix);
    st->decl[i].res = MAKE_RES(1);
    st->decl[i].vt = V_UNSIGNED;
    st->decl[i].in = 1;
    st->decl[i].out = 0;
    st->decl[i].ca.id = idx;
    st->decl[i].ca.bit = p0;
    st->decl[i].ca.len = MAKE_CAN_LEN((p1-p0)+1);
    if (st->ni >= MAX_INPUTS) return BAD_INDEX;    
    st->input[st->ni++] = ix;
    return ix;
}

// make legacy CAN rule for one set or clr expression
// <ox> = <kx> ? (<idx>[p0] == cx)
int make_can_rule(csp_rt_t* st, index_t ox, index_t kx, index_t idx,
		  int byte, int bit, index_t cx)
{
    index_t ex = 0;
    index_t zx = 0;
    index_t qx;
    int p0 = byte*8 + bit;

    // first build condition (can bit test)
    if ((zx = lookup_can_range(st, idx, p0, p0)) == BAD_INDEX) {
	if ((zx = make_can_range(st, NULL, 0, idx, p0, p0)) == BAD_INDEX)
	    return -1;
    }
    if ((zx = new_expr2(st, OP_EQEQ, zx, cx)) == BAD_INDEX)
	return -1;
    // now build the query node (y=ex is computed after query node)
    if ((qx = new_expr2(st, OP_RULE, 0, zx)) == BAD_INDEX)
	return -1;
    st->cond = 1;
    ex = new_expr2(st,OP_EQ,ox,kx);
    st->cond = 0;
    if (ex == BAD_INDEX)
	return -1;
    st->instr[qx].y = ex;
    return 0;
}

// FrameID BytePos Mask OnBits OffBits
int csp_parse_legacy(csp_rt_t* st, tok_t* tok, tokval_t* val, size_t n)
{
    index_t out;
    index_t idx;        
    ivalue_t pos;    
    ivalue_t mask;
    ivalue_t on_bits;
    ivalue_t off_bits;    
    int i;

    if (!expect(tok, 0, INT, INT, INT, INT, INT, LAST))
	return -1;
    pos = val[1].val.i;
    mask = val[2].val.i;
    on_bits = val[3].val.i;
    off_bits = val[4].val.i;
    
    if ((out = lookup_decl(st, "OUT", 3)) == BAD_INDEX) {
	// fixme: pin number etc for standard OUT
	if ((out = csp_new_decl(st,"OUT",3,DECL_DIGITAL)) == BAD_INDEX)
	    return -1;
    }

    if ((idx = lookup_const(st, V_INTEGER, val[0].val)) == BAD_INDEX)
	idx = new_signed_const(st,val[0].val.i);    
    

    // OUT = 1
    for (i = 7; i >= 0; i--) {
	uint8_t bit = (1 << i);
	if ((mask & bit) && (on_bits & bit)) {
	    if (make_can_rule(st, out, ONE, idx, pos, i, ONE) < 0)
		return -1;
	}
    }

    // OUT = 0
    for (i = 7; i >= 0; i--) {
	uint8_t bit = (1 << i);	
	if ((mask & bit) && !(off_bits & bit)) {
	    if (make_can_rule(st, out, ZERO, idx, pos, i, ZERO) < 0)
		return -1;
	}
    }
    return 0;
}

// '>' command
int csp_parse_immediate(csp_rt_t* st, tok_t* tok, tokval_t* val, size_t n)
{
    return 0;
}
    
int csp_parse(csp_rt_t* st, char* str)
{
    tokval_t val[MAX_LINE_TOKENS];
    tok_t tok[MAX_LINE_TOKENS];
    size_t num = MAX_LINE_TOKENS;
    int n;
        
    while((n = csp_scan_line(str, tok, val, &num)) > 0) {
	int r = -1;
	str += n;
	if (tok[0] == NEWLINE)
	    r = 0;
	else if (expect(tok, 0, INT, INT, INT, INT, INT, LAST)) {
	    r = csp_parse_legacy(st, tok, val, num);
	}
	else if (tok[0] == HASH) {
	    switch(tok[1]) {
	    case MODULE:  // '#' 'module' WORD
		r = csp_parse_module(st, tok, val, num);
		break;
	    case END:
		r = csp_parse_end(st, tok, val, num);
		break;
	    case VARIABLE:
		r = csp_parse_variable(st, tok, val, num);
		break;
	    case CONSTANT:
		r = csp_parse_constant(st, tok, val, num);
		break;
	    case DIGITAL:
		r = csp_parse_digital(st, tok, val, num);
		break;
	    case ANALOG:
		r = csp_parse_analog(st, tok, val, num);
		break;
	    case TIMER:
		r = csp_parse_timer(st, tok, val, num);
		break;
	    case CAN:
		r = csp_parse_can(st, tok, val, num);
		break;
	    case WORD: // module instantiation?
		r = csp_parse_mod(st, tok, val, num);
		break;
	    default:
		return -1;
	    }
	}
	else if (tok[0] == GT) {
	    r = csp_parse_immediate(st, tok, val, num);
	}
	else {
	    r = csp_parse_rule(st, tok, val, num);
	}
	if (r < 0)
	    return -1;
	st->line++;
	num = MAX_LINE_TOKENS;
    }
    return 0;
}

void csp_rt_init(csp_rt_t* st)
{
    memset(st, 0x00, sizeof(csp_rt_t));
    st->nn = 0;
    st->nd = 0;
    st->ni = 0;
    st->no = 0;
    st->strp = MAX_STR_BUF;
    st->str[0] = 0;  // reserved 0 and nil
    st->user_funcs = NULL;
    st->num_user_funcs = 0;
    new_signed_const(st, 0);
    new_signed_const(st, 1);
}

// Set user function table (called before parsing)
void csp_set_user_funcs(csp_rt_t* st, const csp_func_t* funcs, uint8_t count)
{
    st->user_funcs = funcs;
    st->num_user_funcs = count;
}

// copy constant and init values 
void csp_rt_start(csp_rt_t* st)
{
    int i;

#if defined(WANT_TRANSACTION) && (WANT_TRANSACTION==1)
    st->up = 0;
#endif
#if defined(WANT_REACTIVE) && (WANT_REACTIVE==1)    
    st->tl = st->hd = 0;
#endif    
    for (i = 0; i < st->nd; i++) {
	switch(st->decl[i].type) {
	case DECL_CONSTANT:
	    st->dval[i] = st->decl[i].cn.init;
	    break;
	case DECL_VARIABLE:
	    csp_set_ivalue(st, MAKE_INDEX(0,i,1), st->decl[i].va.init.i);
	    break;
	case DECL_TIMER:
	    if (st->decl[i].tm.init == 1) {
		int tj = st_index(st, st->decl[i].tm.tx);
		st->decl[i].tm.running = 1;
		st->dval[tj].u = csp_time_ms();
		csp_set_ivalue(st, MAKE_INDEX(0,i,1), 1);
	    }
	    break;
	default:
	    break;
	}
    }
}

int csp_set_transaction(csp_rt_t* st, int onoff)
{
#if defined(WANT_TRANSACTION) && (WANT_TRANSACTION==1)    
    st->transaction = onoff;
    return 0;
#endif
    return -1;
}

int csp_set_reactive(csp_rt_t* st, int onoff)
{
#if defined(WANT_REACTIVE) && (WANT_REACTIVE==1)    
    st->reactive = onoff;
    return 0;
#endif
    return -1;    
}
