// parse and eval
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>

#include "csp.h"

#define CAT_HELPER2(x,y) x ## y
#define CAT2(x,y) CAT_HELPER2(x,y)

// assoc
#define LEFT -1
#define RIGHT 1
#define NO    0
// func
#define TRUE  -1  // all bits set, like openCL/Forth
#define FALSE 0

#define OPENT(o,c,n) \
    [(o)] = { .tok=(o),.code=(c),.name=(n),.name_len=strlen((n)),.arity=-1,.prec=-1,.assoc=NO,.isfunc=FALSE,.isdecl=FALSE,.isinstr=FALSE }

#define INSTR_ENT(o,c,n,a,p,s) \
    [(o)] = { .tok=(o),.code=(c),.name=(n),.name_len=strlen((n)),.arity=(a),.prec=(p),.assoc=(s),.isfunc=FALSE,.isdecl=FALSE,.isinstr=TRUE }

#define FUNC_ENT(o,c,n,a) \
    [(o)] = { .tok=(o),.code=(c),.name=(n),.name_len=strlen((n)),.arity=(a),.prec=-1,.assoc=NO,.isfunc=TRUE,.isdecl=FALSE,.isinstr=FALSE }

#define DECL_ENT(o,c,n) \
    [(o)] = { .tok=(o),.code=(c),.name=(n),.name_len=strlen((n)),.arity=-1,.prec=-1,.assoc=NO,.isfunc=FALSE,.isdecl=TRUE,.isinstr=FALSE }

const struct {
    tok_t  tok;
    int8_t code;
    const char* name;
    int8_t name_len;
    int8_t arity;
    int8_t prec;
    int8_t assoc;
    int8_t isfunc;
    int8_t isdecl;
    int8_t isinstr;    
} op_table[] = {
    OPENT(NONE,OP_NOP,"\0"),
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
    // funcs
    FUNC_ENT(MIN,OP_MIN,"min",2),
    FUNC_ENT(MAX,OP_MAX,"max",2),
    FUNC_ENT(ABS,OP_ABS,"abs",1),
    FUNC_ENT(SIGN,OP_SIGN,"sign",1),
    FUNC_ENT(TIMEOUT,OP_TIMEOUT,"timeout",1),
    FUNC_ENT(PRINT,OP_PRINT,"print",1),
    FUNC_ENT(TICK,OP_TICK,"tick",0),
    FUNC_ENT(CYCLE,OP_CYCLE,"cylce",0),

    // keywords
    OPENT(PULLUP,OP_NOP,"pullup"),
    OPENT(PULLDOWN,OP_NOP,"pulldown"),
    OPENT(RESOLUTION,OP_NOP,"resolution"),
    OPENT(IN,OP_NOP,"in"),
    OPENT(OUT,OP_NOP,"out"),
    OPENT(INOUT,OP_NOP,"inout"),
    OPENT(PWM,OP_NOP,"pwm"),
    OPENT(FLOAT,OP_NOP,"float"),
    OPENT(INTEGER,OP_NOP,"integer"),
    OPENT(UNSIGNED,OP_NOP,"unsigned"),
    OPENT(STRING,OP_NOP,"string"),
    OPENT(LITTLE,OP_NOP,"little"),
    OPENT(BIG,OP_NOP,"big"),
    
    // tokens
    OPENT(LP,OP_NOP,"("),
    OPENT(RP,OP_NOP,")"),
    OPENT(HASH,OP_NOP,"#"),
    OPENT(DOT,OP_NOP,"."),
    OPENT(COLON,OP_NOP,":"),
    OPENT(LB,OP_NOP,"["),
    OPENT(RB,OP_NOP,"]"),
    OPENT(INT,OP_NOP,""),
    OPENT(FLT,OP_NOP,""),
    OPENT(WORD,OP_NOP,""),
    OPENT(NEWLINE,OP_NOP,"\n"),
    // eot
    OPENT(LAST,OP_NOP,"<last>")
};

// fixme: table
tok_t opcode_to_tok(opcode_t opcode)
{
    int i = 0;
    while(op_table[i].tok != LAST) {
	if ((op_table[i].isfunc || op_table[i].isinstr) &&
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

static inline const char* op_name(opcode_t op)
{
    tok_t tok = opcode_to_tok(op);
    return op_table[tok].name;
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

static inline int is_func(tok_t op)
{
    return op_table[op].isfunc;
}

const char* tag(csp_rt_t* st, index_t n)
{
    if (IS_INSTR(n))
	return "i";
    else {
	switch(st->decl[INDEX(n)].type) {
	case DECL_MOD: return "q";
	case DECL_MODULE: return "m";
	case DECL_CONSTANT: return "c";
	case DECL_VARIABLE: return "v";
	case DECL_DIGITAL: return "d";
	case DECL_ANALOG: return "a";
	case DECL_TIMER: return "t";
	case DECL_CAN: return "k";
	case DECL_UART: return "u";
	case DECL_SOCKET: return "s";
	default: return "?";
	}
    }
}

void print_tag(FILE* f, csp_rt_t* st, index_t n)
{
    int m = MOD(n);
    int ix = INDEX(n);
    if (m == 0) // global
	fprintf(f, "%s:%d", tag(st,n), ix);
    else if (m == ANY_MOD) // match
	fprintf(f, "*:%s:%d", tag(st,n), ix);
    else
	fprintf(f, "%s:%d.%d", tag(st,n), m, ix);
}

#if 0
void csp_print_expr(FILE* f, csp_rt_t* st, index_t ix)
{
    if (IS_DECL(ix)) {
	switch(st->decl[INDEX(ix)].type) {
	case DECL_VARIABLE: fprintf(f, "%s", decl_name(st, ix)); break;
	case DECL_CONSTANT: fprintf(f, "%d", st->decl[INDEX(ix)].cn.init.i); break;
	    // FIXME:
	default: fprintf(f, "?"); break;
	}
    }
    else {
	fprintf(f, "(");
	csp_print_expr(f, st, st->instr[INDEX(ix)].y);
	fprintf(f, "%s", op_name(st->instr[INDEX(ix)].op));
	csp_print_expr(f, st, st->instr[INDEX(ix)].z);
	fprintf(f, ")");
    }
}
#endif

// convert integer to -1 if y != 0  0 otherwise
#define BOOL(y) (-((y)!=0))

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
#define op_MIN(y, z)  imin((y),(z))
#define op_MAX(y, z)  imax((y),(z))
#define op_SLA(y, z)  ((y) << (z))
#define op_SRA(y, z)  ((y) >> (z))
#define op_NOT2(y,z)  (~BOOL((y)))
#define op_NEG2(y,z)  (-(y))
#define op_POS2(y,z)  (y)
#define op_INV2(y,z)  (~(y))
#define op_ABS2(y,z)  iabs((y))
#define op_SIGN2(y,z) isign((y))
#define op_COMMA(y,z) z

#define op_NOT(y)     (~BOOL((y)))
#define op_NEG(y)     (-(y))
#define op_POS(y)     (y)
#define op_INV(y)     (~(y))
#define op_ABS(y)     iabs((y))
#define op_SIGN(y)    isign((y))

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
MAKE_OP2(MIN);
MAKE_OP2(MAX);
MAKE_OP2(SLA);
MAKE_OP2(SRA);
MAKE_OP2(ABS2);
MAKE_OP2(SIGN2);
MAKE_OP2(INV2);
MAKE_OP2(NEG2);
MAKE_OP2(POS2);
MAKE_OP2(NOT2);
MAKE_OP2(COMMA);

static ivalue_t (*eval_op2_tab[])(ivalue_t y, ivalue_t z) =
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
    [OP_MIN] = f_MIN,
    [OP_MAX] = f_MAX,
    // unary versions
    [OP_ABS] = f_ABS2,
    [OP_SIGN] = f_SIGN2,    
    [OP_INV] = f_INV2,
    [OP_NEG] = f_NEG2,
    [OP_POS] = f_POS2,    
    [OP_NOT] = f_NOT2,
    // other
    [OP_COMMA] = f_COMMA,
};

#if 0

#define MAKE_OP1(name)					\
    static ivalue_t CAT2(f_,name)(ivalue_t y)		\
    {							\
	return CAT2(op_,name)(y);			\
    }

MAKE_OP1(ABS);
MAKE_OP1(SIGN);
MAKE_OP1(INV);
MAKE_OP1(NEG);
MAKE_OP1(POS);
MAKE_OP1(NOT);

static ivalue_t (*eval_op1_tab[])(ivalue_t y) =
{
    [OP_NOT] = f_NOT,
    [OP_NEG] = f_NEG,
    [OP_POS] = f_POS,
    [OP_INV] = f_INV,
    [OP_ABS] = f_ABS,
    [OP_SIGN] = f_SIGN,
};
#endif

#if 0
static inline ivalue_t eval_op1(opcode_t op, ivalue_t y)
{
    switch(op) {
    case OP_ABS:  return op_ABS(y);
    case OP_SIGN: return op_SIGN(y);
    case OP_NEG:  return op_NEG(y);
    case OP_POS:  return op_POS(y);	
    case OP_NOT:  return op_NOT(y);
    case OP_INV:  return op_INV(y);
    default:   return 0;
    }
}
#endif

static int print_value(FILE* f, csp_rt_t* st, vtype_t vt, value_t val)
{
    switch(vt) {
    case V_INTEGER: return fprintf(f, "%d", val.i);
    case V_UNSIGNED: return fprintf(f, "%u", val.u);
    case V_FLOAT: return fprintf(f, "%f", val.f);
    case V_STRING: return fprintf(f, "%s", &st->str[val.s]);
    default: return fprintf(f, "???");
    }
}

#if 0
static inline ivalue_t eval_op2(tok_t op, ivalue_t y, ivalue_t z)
{
    switch(op) {	
    case OP_ADD:  return op_ADD(y,z);
    case OP_SUB:  return op_SUB(y,z);
    case OP_MUL:  return op_MUL(y,z);
    case OP_DIV:  return op_DIV(y,z);
    case OP_REM:  return op_REM(y,z);
    case OP_SLA:  return op_SLA(y,z);
    case OP_SRA:  return op_SRA(y,z);
    case OP_AND:  return op_AND(y,z);
    case OP_OR:   return op_OR(y,z);
    case OP_XOR:  return op_XOR(y,z);
    case OP_ANDAND:  return op_ANDAND(y,z);
    case OP_OROR:   return op_OROR(y,z);	
    case OP_LT:   return op_LT(y,z);
    case OP_LTE:  return op_LTE(y,z);
    case OP_GT:   return op_GT(y,z);
    case OP_GTE:  return op_GTE(y,z);
    case OP_EQEQ: return op_EQEQ(y,z);
    case OP_NEQ:  return op_NEQ(y,z);
    case OP_MIN:  return op_MIN(y,z);
    case OP_MAX:  return op_MAX(y,z);
	// unary op as binary
    case OP_ABS:  return op_ABS(y);
    case OP_SIGN: return op_SIGN(y);	
    case OP_NEG:  return op_NEG(y);
    case OP_POS:  return op_POS(y);	
    case OP_NOT:  return op_NOT(y);
    case OP_INV:  return op_INV(y);
	// other
    case OP_COMMA: return z;
    default:   return 0;
    }
}
#endif

// #define EVAL_OP1(op,y) eval_op1((op),(y))
// #define EVAL_OP2(op,y,z) eval_op2((op),(y),(z))
#define EVAL_OP1(op,y)   eval_op1_tab[(op)]((y))
#define EVAL_OP2(op,y,z) eval_op2_tab[(op)]((y),(z))

int csp_eval0(csp_rt_t* st, int n)
{
    ivalue_t v;
    value_t vv;
    // fixme: assert IS_INSTR(n) mod=0
    opcode_t op = st->instr[n].op;

    printf("eval0: %d\n", n);
#ifdef WANT_STATISTICS
    st->num_eval0++;
#endif
    switch(op) {
    case OP_RULE:
#ifdef WANT_REACTIVE
	if (st->reactive) {
	    if (csp_ivalue(st,st->instr[n].z))
		csp_enq(st, st->instr[n].y);
	    else
		csp_enq(st, MAKE_INDEX(0,n+1,TAG_INSTR));
	}
	return n+1;
#else
	if (csp_ivalue(st,st->instr[n].z))
	    return n+1;
	else
	    return INDEX(st->instr[n].y)+1;
#endif
	break;
    case OP_ENTER: // skip y + 2 
	return n + st->instr[n].y + 2;
    case OP_NEW:
	if (!st->reactive) {
	    index_t ent = st->instr[n].y;
	    index_t mod = st->instr[n].z;
	    // in non-reactive mode this is like a call
	    st->stack[st->esp].ix = n+1;
	    st->stack[st->esp].so = st->so;
	    st->so = st->instr[n].z;
	    printf("%s = new %s so=%d, ix=%d, so'=%d\n",
		   decl_name(st, mod),
		   decl_name(st, st->decl[INDEX(mod)].mq.mx),
		   st->stack[st->esp].so,
		   st->stack[st->esp].ix,
		   st->so);
	    st->esp++;
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
	    printf("leave so=%d, ix=%d\n",
		   st->stack[st->esp].so,
		   st->stack[st->esp].ix);
	    return n;
	}
	return n+1;
    case OP_EQ: // plain assign
	vv = csp_value(st,st->instr[n].z);
	csp_set_value(st,st->instr[n].y,vv);
	csp_set_value(st,n,vv);
	break;
    case OP_TIMEOUT:
	v = !st->decl[st->instr[n].y].tm.running;
	csp_set_ivalue(st,n,v);
	break;
    case OP_TICK:
	csp_set_ivalue(st,n,csp_time_ms());
	break;
    case OP_CYCLE:
	csp_set_ivalue(st,n,st->cycle);
	break;
    case OP_PRINT: {
	int y = st_index(st, st->instr[n].y);
	if (IS_DECL(st->instr[n].y)) {
	    v = print_value(stdout, st, st->decl[y].vt, st->dval[y]);
	}
	else {
	    // node value need a vtype tag!
	    v = print_value(stdout, st, V_INTEGER, st->xval[y]); 
	}
	csp_set_ivalue(st,n,v);	
	break;
    }
    default:
	if ((op >= 0) && (op < OP_LAST)) {
	    ivalue_t yv = csp_ivalue(st, st->instr[n].y);
	    ivalue_t zv = csp_ivalue(st, st->instr[n].z);
	    v = EVAL_OP2(op, yv, zv);
	    csp_set_ivalue(st,n,v);
	}
	break;
    }
    return n+1;
}

// undo all values
void csp_undo(csp_rt_t* st)
{
#ifdef WANT_TRANSACTION
    int i;
    if (st->transaction) {
	for (i = st->up; i >= 0; i--) {
	    index_t x = st->undo[i].x;
	    int j = st_index(st, x);
	    bitset_clr(st->set,x);
	    if (IS_DECL(x))
		st->dval[j] = st->undo[i].v;
	    else
		st->xval[j] = st->undo[i].v;
	}
	st->up = 0;
    }
#endif
}

void csp_commit(csp_rt_t* st)
{
#ifdef WANT_TRANSACTION
    if (st->transaction) {
	bitset_zero(st->set);  // commit settings
	st->up = 0; // reset
    }
#endif
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
    while(n < st->nn) {
	n = csp_eval0(st, n);
    }
    return x;
}

// run queue until empty
index_t csp_react(csp_rt_t* st)
{
    st->cycle++;
#ifdef WANT_REACTIVE
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
    
//    printf("lookup_decl: len=%d, %.*s start=%d, stop=%d\n",
//	   name_len, name_len, name, start, stop);
    
    while(i < stop) {
	int pos = st->decl[i].name;
	int len = st->str[pos-1];
	// printf("cmp_decl: len=%d %.*s\n",
	// len, len, decl_name(st, MAKE_INDEX(0,i,TAG_DECL)));
	if ((len == name_len) &&
	    (memcmp(decl_name(st, MAKE_INDEX(0,i,TAG_DECL)),
		    name, name_len)==0)) {
	    // printf("found: pos=%d\n", i);
	    return MAKE_INDEX(0, i, TAG_DECL);
	}
	if (st->decl[i].type == DECL_MODULE) // skip module def
	    i += (st->decl[i].md.n+1); // skip elements and END
	i++;
    }
    // printf("not found\n");
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
	ivalue_t n = st->decl[mx].md.n;  // number of elements
	if (mx == BAD_INDEX)
	    return BAD_INDEX;
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
	// printf("new_decl len=%d %.*s i=%d\n", name_len,
	//     name_len, name, INDEX(i));
	if ((pos = new_string(st, name, name_len)) < 0)
	    return BAD_INDEX;
    }
    else {
	// printf("new decl i=%d\n", INDEX(i));
    }
    st->decl[INDEX(i)].type = type;    
    st->decl[INDEX(i)].name = pos;
    st->decl[INDEX(i)].vt = V_INTEGER;
    return i;
}

index_t new_signed_const(csp_rt_t* st, ivalue_t v)
{
    index_t ix;
    if ((ix = csp_new_decl(st,NULL,0,DECL_CONSTANT)) == BAD_INDEX)
	return BAD_INDEX;
    st->decl[INDEX(ix)].res = MAKE_RES(8*sizeof(ivalue_t));
    st->decl[INDEX(ix)].vt = V_INTEGER;
    st->decl[INDEX(ix)].cn.init.i = v;
    return ix;
}

index_t new_float_const(csp_rt_t* st, fvalue_t v)
{
    index_t ix;
    if ((ix = csp_new_decl(st,NULL,0,DECL_CONSTANT)) == BAD_INDEX)
	return BAD_INDEX;
    st->decl[INDEX(ix)].res = MAKE_RES(8*sizeof(fvalue_t));
    st->decl[INDEX(ix)].vt = V_FLOAT;
    st->decl[INDEX(ix)].cn.init.f = v;
    return ix;
}

index_t new_string_const(csp_rt_t* st, char* str, int len)
{
    index_t ix;
    int pos;    
    if ((ix = csp_new_decl(st,NULL,0,DECL_CONSTANT)) == BAD_INDEX)
	return BAD_INDEX;
    if ((pos = new_string(st, str, len)) < 0)
	return BAD_INDEX;	
    st->decl[INDEX(ix)].res = MAKE_RES(STRING_BITS);
    st->decl[INDEX(ix)].vt = V_STRING;
    st->decl[INDEX(ix)].cn.init.s = pos;
    return ix;
}

index_t csp_new_node(csp_rt_t* st, opcode_t op, index_t y, index_t z)
{
    index_t x;
    if ((x = next_instr_index(st)) == BAD_INDEX)
	return BAD_INDEX;    
    st->instr[INDEX(x)].op = op;
    st->instr[INDEX(x)].cond = st->cond;
    st->instr[INDEX(x)].y = y;
    st->instr[INDEX(x)].z = z;
    return x;
}

index_t new_expr2(csp_rt_t* st, opcode_t op, index_t y, index_t z)
{
    if ((op == OP_EQ) && IS_CONST(st,y))
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

static int unpack_args(csp_rt_t* st, index_t arg, index_t* argv, int max_args)
{
    int i = 0;
    
    while (st->instr[arg].op == OP_COMMA) {
	if (i >= max_args)
	    return -1;
	argv[i++] = st->instr[arg].y;
	arg = st->instr[arg].z;
    }
    argv[i++] = arg;
    return i;
}

index_t new_call(csp_rt_t* st, opcode_t op, index_t y)
{
    tok_t tok = opcode_to_tok(op);    
    int argc = arity(tok);
    int n;
    index_t argv[MAX_ARGS];

    if (argc > MAX_ARGS)
	return PARSE_ERROR;
    n = unpack_args(st, y, argv, MAX_ARGS);
    if ((n < 0) || (n != argc))
	return PARSE_ERROR;	
    switch(n) {
    case 2: return new_expr2(st,op,argv[0],argv[1]);
    case 1: return new_expr1(st,op,argv[0]);
    case 0: return new_expr0(st,op);
    default: return PARSE_ERROR;
    }
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
#ifdef WANT_REACTIVE
    int i, x0, x1;
    int in_module = 0;
    index_t wr[MAX_INDEX];

    // setup idg for all nodes
    // x : y op z   count y
    // 
    for (i = 0; i < st->nn; i++) {
	if (IS_INSTR(i)) {
	    index_t y = st->instr[i].y;
	    index_t z = st->instr[i].z;
	    if (DEP(st,i,y))
		st->idg[INDEX(y)]++;
	    if (DEP(st,i,z))
		st->idg[INDEX(z)]++;
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

typedef union
{
    struct {
	char* str;
	int len;
    };
    value_t val;
} tokval_t;

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
	    while((isalpha(*str)||isdigit(*str)) && (len < MAX_NAME_LEN)) {
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
	while(pp && ((op = ostack[pp-1]) != LP)) {
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
	pp--;
	if (pp && is_func(ostack[pp-1])) {
	    op = ostack[--pp];
	    ix = new_call(st, op_table[op].code, xstack[ep-1]);
	    if (ix == BAD_INDEX) return PARSE_ERROR;
	    xstack[ep-1] = ix;
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
    case WORD:
	if ((ix = lookup_decl(st,tval.str,tval.len)) == BAD_INDEX) {
	    if ((ix = csp_new_decl(st,tval.str,tval.len,DECL_VARIABLE)) == BAD_INDEX)
		return PARSE_ERROR;
	    st->decl[INDEX(ix)].va.init.i = 0;
	}
	else if ((st->decl[INDEX(ix)].type == DECL_MOD) &&
		 (tok[i] == DOT) && (tok[i+1] == WORD)) {
	    index_t mx = st->decl[INDEX(ix)].mq.mx; // module def
	    ivalue_t n = st->decl[INDEX(mx)].md.n;  // number of elements 
	    index_t jx;

	    tval = val[i+1];
	    if ((jx = lookup_decl_in(st,tval.str,tval.len,
				     INDEX(mx)+1,INDEX(mx)+1+n)) == BAD_INDEX)
		return PARSE_ERROR;
	    ix = MAKE_INDEX(st->decl[INDEX(ix)].mq.iq,jx,TAG_DECL);
	    i += 2;
	}
	xstack[ep++] = ix;
	pop = WORD;
	break;
    default:
	// check for function arity 1,2
	if ((op_table[op].arity >= 0) &&
	    (op_table[op].prec == -1)) {
	    ostack[pp++] = (pop = op);
	}
	else {
	    return PARSE_ERROR;
	}
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


// '#' 'module' <name>
int csp_parse_module(csp_rt_t* st, tok_t* tok, tokval_t* val, size_t n)
{
    index_t ix;
    index_t jx;
    
    if (tok[0] != HASH) return -1;
    if (tok[1] != MODULE) return -1;
    if (tok[2] != WORD) return -1;
    
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
    
    if (tok[0] != HASH) return -1;
    if (tok[1] != END) return -1;

    if ((mx = st->mdef)) {
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

    if (tok[0] != HASH) return -1;
    if (tok[1] != VARIABLE) return -1;    
    if (tok[2] != WORD) return -1;
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
    st->decl[INDEX(ix)].vt = vt;
    st->decl[INDEX(ix)].res = res;
    st->decl[INDEX(ix)].in = in;
    st->decl[INDEX(ix)].out = out;    
    st->decl[INDEX(ix)].va.init = def;
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

    if (tok[0] != HASH) return -1;
    if (tok[1] != CONSTANT) return -1;    
    if (tok[2] != WORD) return -1;
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
    st->decl[INDEX(ix)].res = res;
    st->decl[INDEX(ix)].vt = vt;
    st->decl[INDEX(ix)].cn.init = cnst;
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
    
    if (tok[0] != HASH) return -1;
    if (tok[1] != DIGITAL) return -1;    
    if (tok[2] != WORD) return -1;

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
    if ((tok[i]==INT) && (tok[i+1] == COLON) && (tok[i+2]==INT)) {
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
    st->decl[INDEX(ix)].res = res;
    st->decl[INDEX(ix)].di.pin = pin;
    st->decl[INDEX(ix)].di.port = port;
    st->decl[INDEX(ix)].in = in;
    st->decl[INDEX(ix)].out = out;
    st->decl[INDEX(ix)].di.pullup = pu;
    st->decl[INDEX(ix)].di.pulldown = pd;
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
    
    if (tok[0] != HASH) return -1;
    if (tok[1] != ANALOG) return -1;    
    if (tok[2] != WORD) return -1;
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
    if ((tok[i] == INT) && (tok[i+1] == COLON) && (tok[i+2]==INT)) {
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
    st->decl[INDEX(ix)].vt = vt;
    st->decl[INDEX(ix)].res = res;
    st->decl[INDEX(ix)].in = in;
    st->decl[INDEX(ix)].out = out;    
    st->decl[INDEX(ix)].an.pin = pin;
    st->decl[INDEX(ix)].an.port = port;
    st->decl[INDEX(ix)].an.pwm = pwm;
    return 0;            
}

// '#' 'timer' <name> <milliseconds> ['=' '1'|'0']
int csp_parse_timer(csp_rt_t* st, tok_t* tok, tokval_t* val, size_t n)
{
    ivalue_t init = 0;
    index_t ix, px, tx;
    
    if (tok[0] != HASH) return -1;
    if (tok[1] != TIMER) return -1;    
    if (tok[2] != WORD) return -1;
    if (tok[3] != INT) return -1;  // allow veriable/constant name 
    if (st->nt >= MAX_TIMERS) return -1;

    if (tok[4] == EQ) {
	if (tok[5] != INT) return -1;
	init = val[5].val.i;
    }

    if ((px = lookup_const(st, V_INTEGER, val[3].val)) == BAD_INDEX)
	px = new_signed_const(st,val[3].val.i);

    tx = csp_new_decl(st, NULL, 0, DECL_VARIABLE);
    st->decl[INDEX(tx)].vt = V_UNSIGNED;
    st->decl[INDEX(tx)].res = 31;
    st->decl[INDEX(tx)].va.init.u = 0;
    
    if ((ix = lookup_decl(st, val[2].str, val[2].len)) == BAD_INDEX) {
	if ((ix = csp_new_decl(st, val[2].str, val[2].len, DECL_TIMER)) == BAD_INDEX)
	    return -1;
    }
    st->timer[st->nt++] = ix;
    st->decl[INDEX(ix)].tm.running = 0;
    st->decl[INDEX(ix)].tm.init = init;
    st->decl[INDEX(ix)].tm.px = px;
    st->decl[INDEX(ix)].tm.tx = tx;
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
    
    if (tok[0] != HASH) return -1;
    if (tok[1] != CAN) return -1;    
    if (tok[2] != WORD) return -1;
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
    st->decl[INDEX(ix)].res = res;
    st->decl[INDEX(ix)].vt = vt;
    st->decl[INDEX(ix)].in = in;
    st->decl[INDEX(ix)].out = out;
    st->decl[INDEX(ix)].ca.id = idx;
    st->decl[INDEX(ix)].ca.bit = bit0;
    st->decl[INDEX(ix)].ca.len = (bit1-bit0);
    st->decl[INDEX(ix)].ca.endian = endian;
    return 0;    
}

// '#' word <name>
int csp_parse_mod(csp_rt_t* st, tok_t* tok, tokval_t* val, size_t n)
{
    index_t mx, ix, jx;
    
    if (tok[0] != HASH) return -1;
    if (tok[1] != WORD) return -1;
    if (tok[2] != WORD) return -1;

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
    st->decl[INDEX(ix)].mq.mx = mx;
    st->decl[INDEX(ix)].mq.iq = st->nq;
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
    if ((qx = new_expr2(st, OP_RULE, FALSE, cx)) == BAD_INDEX)
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
    ix = csp_new_decl(st, str, len, DECL_CAN);
    if (ix == BAD_INDEX) return BAD_INDEX;
    st->decl[INDEX(ix)].res = MAKE_RES(1);
    st->decl[INDEX(ix)].vt = V_UNSIGNED;
    st->decl[INDEX(ix)].in = 1;
    st->decl[INDEX(ix)].out = 0;
    st->decl[INDEX(ix)].ca.id = idx;
    st->decl[INDEX(ix)].ca.bit = p0;
    st->decl[INDEX(ix)].ca.len = MAKE_CAN_LEN((p1-p0)+1);
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
    
    if (tok[0] != INT) return -1; 
    if (tok[1] != INT) return -1;
    pos = val[1].val.i;
    if (tok[2] != INT) return -1;
    mask = val[2].val.i;
    if (tok[3] != INT) return -1;
    on_bits = val[3].val.i;
    if (tok[4] != INT) return -1;
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
	else if ((tok[0] == INT) && (tok[1] == INT) &&
		 (tok[2] == INT) && (tok[3] == INT) && (tok[4] == INT)) {
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

void dump_edge_list(FILE* f, csp_rt_t* st, int i)
{
#ifdef WANT_REACTIVE
    if (st->idg[i]) {
	int j;
	fprintf(f, ",e=");
	for (j = 0; j < st->idg[i]; j++) {
	    index_t p = st->edg[st->ofs[i]+j];  // parent node
	    print_tag(f, st, p);
	    fprintf(f, ",");
	}
    }
#endif
}

static const char* fmt_pindir(csp_decl_t* lp)
{
    if (lp->in && lp->out)
	return " inout";
    else if (lp->in)
	return " in";
    else if (lp->out)
	return " out";
    else
	return "";
}

static const char* fmt_pull(csp_rt_t* st, int ix)
{
    if (st->decl[ix].di.pullup)
	return " pullup";
    else if (st->decl[ix].di.pulldown)
	return " pulldown";
    else
	return "";
}

static const char* fmt_pwm(csp_rt_t* st, int ix)
{
    if (st->decl[ix].an.pwm)
	return " pwm";
    else
	return "";
}

static const char* fmt_vtype(vtype_t vt)
{
    switch(vt) {
    case V_FLOAT: return "float";
    case V_UNSIGNED: return "unsigned";
    case V_INTEGER: return "integer";
    case V_STRING: return "string";	
    default: return "";
    }
}

static const char* fmt_endian(vendian_t et)
{
    switch(et) {
    case E_LITTLE: return "little";
    case E_BIG: return "big";
    default: return "";
    }
}

index_t csp_dump_instr(FILE* f, int lev, csp_rt_t* st, int i)
{
    int cond = st->instr[i].cond;
    int vt = st->instr[i].vt;
    
    fprintf(f, "%-*s %s%-4d: ", 2*lev, " ", (cond ? "*" : " "), i);

    switch(st->instr[i].op) {
    case OP_ENTER: {
	index_t mx = st->instr[i].z;
	fprintf(f, " enter %s, n=%d", decl_name(st, mx), st->instr[i].y);
	break;
    }
    case OP_LEAVE: {
	index_t mx = st->instr[i].z;	
	fprintf(f, " leave %s, n=%d", decl_name(st,mx), st->instr[i].y);
	break;
    }
    case OP_NEW: {
	index_t ent = st->instr[i].y;
	index_t mod = st->instr[i].z;
	index_t mx  = st->decl[INDEX(mod)].mq.mx;
	int mi = st->decl[INDEX(mod)].mq.iq;
	int ofs = st->mofs[mi];
	fprintf(f, " %s = new(%s) ent=%d, ofs=%d",
		decl_name(st, mod), decl_name(st, mx), INDEX(ent), ofs);
	break;
    }
    default:
	print_tag(f, st, st->instr[i].y);
	fprintf(f, " '%s' ", op_name(st->instr[i].op));
	print_tag(f, st, st->instr[i].z);
    }
    if (st) {
	fprintf(f, " [");
	print_value(f, st, vt, st->xval[i]);
	fprintf(f, "]");
    }
    dump_edge_list(f, st, i);	
    fprintf(f, "\n");
    return i+1;
}

index_t csp_dump_decl(FILE* f, int lev, csp_rt_t* st, int i)
{
    index_t ix = MAKE_INDEX(0,i,TAG_DECL);
    int vt = V_INTEGER;
    fprintf(f, "%-*s %-4d: ", 2*lev, " ", i);
    switch(st->decl[i].type) {
    case DECL_MODULE: {
	index_t n = st->decl[i].md.n;
	fprintf(f, "#module %s, n=%d\n", decl_name(st, ix), n);
	i++;
	while(n--) {
	    i = csp_dump_decl(f, lev+1, st, i);
	}
	return i;
    }
    case DECL_END:
	fprintf(f, "#end");
	break;
    case DECL_MOD:
	fprintf(f, "#%s %s",
		decl_name(st, st->decl[i].mq.mx),
		decl_name(st, ix));
	break;
    case DECL_VARIABLE:
	vt = st->decl[i].vt;
	fprintf(f, "#variable %s:%d%s %s=",
		decl_name(st, ix),
		GET_RES(st->decl[i].res),
		fmt_pindir(&st->decl[i]),
		fmt_vtype(vt));
	print_value(f, st, vt, st->decl[i].va.init);
	break;
    case DECL_CONSTANT:
	vt = st->decl[i].vt;	    
	fprintf(f, "#constant %s:%d %s=",
		decl_name(st, ix),
		GET_RES(st->decl[i].res),
		fmt_vtype(vt));
	print_value(f, st, vt, st->decl[i].cn.init);
	break;
    case DECL_DIGITAL:
	vt = st->decl[i].vt; // should be unsigned
	fprintf(f, "#digital %s%s%s %d:%d",
		decl_name(st, ix),
		fmt_pindir(&st->decl[i]),
		fmt_pull(st, i),
		st->decl[i].di.port, st->decl[i].di.pin);
	break;
    case DECL_ANALOG:
	vt = st->decl[i].vt;	    
	fprintf(f, "#analog %s:%d %s%s%s %d:%d",
		decl_name(st, ix),
		GET_RES(st->decl[i].res),
		fmt_vtype(vt),
		fmt_pindir(&st->decl[i]),
		fmt_pwm(st, i),
		st->decl[i].an.port, st->decl[i].an.pin);
	break;
    case DECL_TIMER:
	vt = st->decl[i].vt;
	fprintf(f, "#timer %s %d signed=%d",
		decl_name(st, ix),
		csp_ivalue(st, st->decl[i].tm.px),
		st->decl[i].tm.init);
	fprintf(f, " t0=");
	print_tag(f, st, st->decl[i].tm.tx);
	break;
    case DECL_CAN:
	vt = st->decl[i].vt;
	fprintf(f, "#can %s:%d %s%s%s 0x%x[%d:%d]",
		decl_name(st, ix),
		GET_RES(st->decl[i].res),
		fmt_vtype(vt),
		fmt_endian(st->decl[i].ca.endian),
		fmt_pindir(&st->decl[i]),
		csp_ivalue(st, st->decl[i].ca.id),
		st->decl[i].ca.bit, GET_CAN_LEN(st->decl[i].ca.len));
	break;
    default:
	break;
    }
    if (st) {
	fprintf(f, " [");
	print_value(f, st, vt, st->dval[i]);
	fprintf(f, "]");	    
    }
    fprintf(f, "\n");
    return i+1;
}

    
void csp_dump(FILE* f, csp_rt_t* st)
{
    int i;

    // decls
    fprintf(f, "DECL %d\n", st->nd);
    i = 0;
    while(i < st->nd) {
	i = csp_dump_decl(f, 1, st, i);
    }
    // instructions
    fprintf(f, "INSTR %d\n", st->nn);
    i = 0;
    while(i < st->nn) {
	i = csp_dump_instr(f, 1, st, i);
    }

    fprintf(f, "INPUTS %d\n", st->ni);
    for (i = 0; i < st->ni; i++) {
	print_tag(f, st, st->input[i]);
	fprintf(f, "\n");
    }

    fprintf(f, "OUTPUTS %d\n", st->no);
    for (i = 0; i < st->no; i++) {
	print_tag(f, st, st->output[i]);
	fprintf(f, "\n");
    }

    fprintf(f, "MODULES %d\n", st->nm);
    for (i = 0; i < st->nm; i++) {
	print_tag(f, st, st->module[i]);
	fprintf(f, "\n");
    }

    fprintf(f, "MODS %d\n", st->nq);
    for (i = 0; i < st->nq; i++) {
	index_t ix = st->mod[i];
	print_tag(f, st, ix);
	fprintf(f, " mod=%s", decl_name(st, st->decl[INDEX(ix)].mq.mx));
	fprintf(f, " offs=%d", st->mofs[i]);
	fprintf(f, "\n");
    }            

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
    new_signed_const(st, 0);
    new_signed_const(st, 1);
}

// copy constant and init values 
void csp_rt_start(csp_rt_t* st)
{
    int i;

#ifdef WANT_TRANSACTION
    st->up = 0;
#endif
#ifdef WANT_REACTIVE
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
#ifdef WANT_TRANSACTION
    st->transaction = onoff;
    return 0;
#endif
    return -1;
}

int csp_set_reactive(csp_rt_t* st, int onoff)
{
#ifdef WANT_REACTIVE
    st->reactive = onoff;
    return 0;
#endif
    return -1;    
}
