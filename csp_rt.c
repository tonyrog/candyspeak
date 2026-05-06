// CandySpeak runtime
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>

#include "csp.h"
#ifdef DEBUG
#include <stdio.h>
#define DEBUG_XYZ
#include "csp_dump.h"
#endif

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
    INSTR_ENT(TILDE,OP_BNOT,"~",1,105,RIGHT),
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
    INSTR_ENT(AMP,OP_BAND,"&",2,50,LEFT),
    INSTR_ENT(CIRC,OP_BXOR,"^",2,40,LEFT),
    INSTR_ENT(BAR,OP_BOR,"|",2,30,LEFT),
    INSTR_ENT(AMPAMP,OP_AND,"&&",2,20,LEFT),
    INSTR_ENT(BARBAR,OP_OR,"||",2,10,LEFT),
    INSTR_ENT(EQ,OP_EQ,"=",2,5,RIGHT),
    INSTR_ENT(COMMA,OP_COMMA,",",2,2,RIGHT),
    INSTR_ENT(QUEST,OP_RULE,"?",-1,-1,NO),

    INSTR_ENT(NEXT,OP_NEXT, "next",-1,-1,NO),    

    // OP_ENTER: y=<num-instr>, z=DECL:module-index
    INSTR_ENT(ENTER,OP_ENTER,"enter",-1,-1,NO),
    // OP_ENTER: y=<num-instr>, z=DECL:module-index
    INSTR_ENT(LEAVE,OP_LEAVE,"leave",-1,-1,NO),
    // OP_NEW: y=INSTR:enter-index, z=DECL:mod-index
    INSTR_ENT(NEW,OP_NEW,"new",-1,-1,NO),
    // functions are now looked up via csp_lookup_func() + csp_builtin_funcs[]
    INSTR_ENT(CALL,OP_CALL,"call",-1,-1,NO),
    INSTR_ENT(LD,OP_LD,"ld",-1,-1,NO),
    INSTR_ENT(ST,OP_ST,"st",-1,-1,NO),
    INSTR_ENT(LDI,OP_LI,"li",-1,-1,NO),
    INSTR_ENT(ARG,OP_ARG,"arg",-1,-1,NO),
    INSTR_ENT(CVTIF,OP_CVTIF,"cvtif",-1,-1,NO),
    INSTR_ENT(CVTIF,OP_CVTFI,"cvtfi",-1,-1,NO),    
    

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
#define op_BAND(y, z)  ((y)&(z))
#define op_BOR(y, z)   ((y)|(z))
#define op_BXOR(y, z)  ((y)^(z))
// logical 1 == -1 (all bits set)
#define op_AND(y, z)  (-((y)&&(z)))
#define op_OR(y, z)   (-((y)||(z)))
#define op_LT(y, z)   (-((y)<(z)))
#define op_LTE(y, z)  (-((y)<=(z)))
#define op_GT(y, z)   (-((y)>(z)))
#define op_GTE(y, z)  (-((y)>=(z)))
#define op_EQEQ(y, z) (-((y)==(z)))
#define op_NEQ(y, z)  (-((y)!=(z)))
#define op_SLA(y, z)  ((y) << (z))
#define op_SRA(y, z)  ((y) >> (z))
#define op_COMMA(y,z) z

#define op_FADD(y, z)  ((y)+(z))
#define op_FSUB(y, z)  ((y)-(z))
#define op_FMUL(y, z)  ((y)*(z))
#define op_FDIV(y, z)  ((y)/(z))
#define op_FLT(y, z)   (-((y)<(z)))
#define op_FLTE(y, z)  (-((y)<=(z)))
#define op_FGT(y, z)   (-((y)>(z)))
#define op_FGTE(y, z)  (-((y)>=(z)))
#define op_FEQEQ(y, z) (-((y)==(z)))
#define op_FNEQ(y, z)  (-((y)!=(z)))

#define op_NOT(y)  (~BOOL((y)))
#define op_NEG(y)  (-(y))
#define op_POS(y)  (y)
#define op_BNOT(y)  (~(y))
#define op_CVTIF(y) ((fvalue_t) (y))
#define op_CVTFI(y) ((ivalue_t) (y))

#define op_FNEG(y)  (-(y))
#define op_FPOS(y)  (y)

#define MAKE_III(name)						\
    static value_t CAT2(f_,name)(value_t y, value_t z)		\
    {								\
        value_t x;						\
	x.i = CAT2(op_,name)(y.i, z.i);				\
	return x;						\
    }

#define MAKE_IFF(name)						\
    static value_t CAT2(f_,name)(value_t y, value_t z)		\
    {								\
        value_t x;						\
	x.i = CAT2(op_,name)(y.f, z.f);				\
	return x;						\
    }

#define MAKE_FFF(name)						\
    static value_t CAT2(f_,name)(value_t y, value_t z)		\
    {								\
        value_t x;						\
	x.f = CAT2(op_,name)(y.f, z.f);				\
	return x;						\
    }

#define MAKE_II(name)						\
    static value_t CAT2(f_,name)(value_t y)			\
    {								\
        value_t x;						\
	x.i = CAT2(op_,name)(y.i);				\
	return x;						\
    }

#define MAKE_FF(name)						\
    static value_t CAT2(f_,name)(value_t y)			\
    {								\
        value_t x;						\
	x.f = CAT2(op_,name)(y.f);				\
	return x;						\
    }

#define MAKE_IF(name)						\
    static value_t CAT2(f_,name)(value_t y)			\
    {								\
        value_t x;						\
	x.f = CAT2(op_,name)(y.i);				\
	return x;						\
    }

#define MAKE_FI(name)						\
    static value_t CAT2(f_,name)(value_t y)			\
    {								\
        value_t x;						\
	x.i = CAT2(op_,name)(y.f);				\
	return x;						\
    }

MAKE_FF(FNEG);

MAKE_III(ADD);
MAKE_III(SUB);
MAKE_III(MUL);
MAKE_III(DIV);
MAKE_III(REM);
MAKE_III(BAND);
MAKE_III(BOR);
MAKE_III(BXOR);
MAKE_III(AND);
MAKE_III(OR);
MAKE_III(LT);
MAKE_III(LTE);
MAKE_III(GT);
MAKE_III(GTE);
MAKE_III(EQEQ);
MAKE_III(NEQ);
MAKE_III(SLA);
MAKE_III(SRA);
MAKE_III(COMMA);

MAKE_II(BNOT);
MAKE_II(NEG);
MAKE_II(POS);
MAKE_II(NOT);
MAKE_IF(CVTIF);
MAKE_FI(CVTFI);

MAKE_FFF(FADD);
MAKE_FFF(FSUB);
MAKE_FFF(FMUL);
MAKE_FFF(FDIV);

MAKE_IFF(FLT);
MAKE_IFF(FLTE);
MAKE_IFF(FGT);
MAKE_IFF(FGTE);
MAKE_IFF(FEQEQ);
MAKE_IFF(FNEQ);


typedef struct {
    char*   name;
    int     arity;
    vtype_t  rtype;
    vtype_t  type[4];
} op_info_t;

// opcode => opcode type info
static op_info_t info_tab[] = {
    [OP_ADD] = {"ADD",2,V_INTEGER,{V_INTEGER,V_INTEGER}},
    [OP_SUB] = {"SUB",2,V_INTEGER,{V_INTEGER,V_INTEGER}},
    [OP_MUL] = {"MUL",2,V_INTEGER,{V_INTEGER,V_INTEGER}},
    [OP_DIV] = {"DIV",2,V_INTEGER,{V_INTEGER,V_INTEGER}},
    [OP_REM] = {"REM",2,V_INTEGER,{V_INTEGER,V_INTEGER}},
    [OP_SLA] = {"SLA",2,V_INTEGER,{V_INTEGER,V_INTEGER}},
    [OP_SRA] = {"SRA",2,V_INTEGER,{V_INTEGER,V_INTEGER}},
    [OP_BAND] = {"BAND",2,V_INTEGER,{V_INTEGER,V_INTEGER}},
    [OP_BOR] = {"BOR",2,V_INTEGER,{V_INTEGER,V_INTEGER}},
    [OP_BXOR] = {"BXOR",2,V_INTEGER,{V_INTEGER,V_INTEGER}},
    [OP_AND] = {"AND",2,V_INTEGER,{V_INTEGER,V_INTEGER}},
    [OP_OR] = {"OR",2,V_INTEGER,{V_INTEGER,V_INTEGER}},
    [OP_EQ] = {"ASSIGN",2,V_INTEGER,{V_INDEX,V_INTEGER}},
    [OP_LT] = {"LT",2,V_INTEGER,{V_INTEGER,V_INTEGER}},
    [OP_LTE] = {"LTE",2,V_INTEGER,{V_INTEGER,V_INTEGER}},
    [OP_GT] = {"GT",2,V_INTEGER,{V_INTEGER,V_INTEGER}},
    [OP_GTE] = {"GTE",2,V_INTEGER,{V_INTEGER,V_INTEGER}},
    [OP_EQEQ] = {"EQ",2,V_INTEGER,{V_INTEGER,V_INTEGER}},
    [OP_NEQ] = {"NEQ",2,V_INTEGER,{V_INTEGER,V_INTEGER}},
    // unary versions (treated as binary with z ignored)
    [OP_BNOT] = {"BNOT",1,V_INTEGER,{V_INTEGER}},
    [OP_NEG] = {"NEG",1,V_INTEGER,{V_INTEGER}},
    [OP_POS] = {"POS",1,V_INTEGER,{V_INTEGER}},
    [OP_NOT] = {"NOT",1,V_INTEGER,{V_INTEGER}},
    [OP_CVTIF] = {"CVTIF",1,V_FLOAT,{V_INTEGER}},   // int→float
    [OP_CVTFI] = {"CVTFI",1,V_INTEGER,{V_FLOAT}},  // float→int

    [OP_FNEG] = {"FNEG",1,V_FLOAT,{V_FLOAT}},    
    [OP_FADD] = {"FADD",2,V_FLOAT,{V_FLOAT,V_FLOAT}},
    [OP_FSUB] = {"FSUB",2,V_FLOAT,{V_FLOAT,V_FLOAT}},
    [OP_FMUL] = {"FMUL",2,V_FLOAT,{V_FLOAT,V_FLOAT}},
    [OP_FDIV] = {"FDIV",2,V_FLOAT,{V_FLOAT,V_FLOAT}},

    [OP_FLT] = {"FLT",2,V_INTEGER,{V_FLOAT,V_FLOAT}},
    [OP_FLTE] = {"FLTE",2,V_INTEGER,{V_FLOAT,V_FLOAT}},
    [OP_FGT] = {"FGT",2,V_INTEGER,{V_FLOAT,V_FLOAT}},
    [OP_FGTE] = {"FGTE",2,V_INTEGER,{V_FLOAT,V_FLOAT}},
    [OP_FEQEQ] = {"FEQ",2,V_INTEGER,{V_FLOAT,V_FLOAT}},
    [OP_FNEQ] = {"FNEQ",2,V_INTEGER,{V_FLOAT,V_FLOAT}},
    
    // comman may not be needed?
    [OP_COMMA] = {"COMMA",2,V_INTEGER,{V_INTEGER,V_INTEGER}},

    // other operations for name
    [OP_ENTER] = {"ENTER",0,V_VOID,{}},
    [OP_LEAVE] = {"LEAVE",0,V_VOID,{}},
    [OP_NEW]   = {"NEW",0,V_VOID,{}},
    [OP_LI]    = {"LI",0,V_VOID,{}},
    [OP_ARG]   = {"ARG",0,V_VOID,{}},    
    [OP_ST]    = {"ST",0,V_VOID,{}},
    [OP_LD]    = {"LD",0,V_VOID,{}},
    [OP_CALL]  = {"CALL",0,V_VOID,{}},
    [OP_RULE]  = {"RULE",0,V_VOID,{}},
    [OP_NEXT]  = {"NEXT",0,V_VOID,{}},
    [OP_NOP] = {"NOP",0,V_VOID,{}},    
    
};
typedef value_t (*eval0_fn)();
typedef value_t (*eval1_fn)(value_t y);
typedef value_t (*eval2_fn)(value_t y, value_t z);

const eval0_fn eval_tab0[] =
{
};

const eval1_fn eval_tab1[] =
{
    [OP_BNOT]  = f_BNOT,
    [OP_NEG]   = f_NEG,
    [OP_POS]   = f_POS,
    [OP_NOT]   = f_NOT,
    [OP_CVTIF] = f_CVTIF,
    [OP_CVTFI] = f_CVTFI,
    [OP_FNEG]   = f_FNEG,    
};

const eval2_fn eval_tab2[] =
{
    [OP_ADD] = f_ADD,
    [OP_SUB] = f_SUB,
    [OP_MUL] = f_MUL,
    [OP_DIV] = f_DIV,
    [OP_REM] = f_REM,
    [OP_SLA] = f_SLA,
    [OP_SRA] = f_SRA,
    [OP_BAND] = f_BAND,
    [OP_BOR] = f_BOR,
    [OP_BXOR] = f_BXOR,
    [OP_AND] = f_AND,
    [OP_OR] = f_OR,
    [OP_LT] = f_LT,
    [OP_LTE] = f_LTE,
    [OP_GT] = f_GT,
    [OP_GTE] = f_GTE,
    [OP_EQEQ] = f_EQEQ,
    [OP_NEQ] = f_NEQ,

    [OP_FADD] = f_FADD,
    [OP_FSUB] = f_FSUB,
    [OP_FMUL] = f_FMUL,
    [OP_FDIV] = f_FDIV,

    [OP_FLT] = f_FLT,
    [OP_FLTE] = f_FLTE,
    [OP_FGT] = f_FGT,
    [OP_FGTE] = f_FGTE,
    [OP_FEQEQ] = f_FEQEQ,
    [OP_FNEQ] = f_FNEQ,    
    
    [OP_COMMA] = f_COMMA,
};

vtype_t csp_opcode_type(opcode_t op)
{
    return info_tab[op].rtype;
}

const char* csp_op_name(opcode_t op)
{
    return info_tab[op].name;
}

void csp_set_error(csp_rt_t* st, csp_err_t err)
{
    st->ps.err = err;
}

void csp_clr_error(csp_rt_t* st)
{
    st->ps.err = ERR_OK;
}

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

static inline ivalue_t iclip(ivalue_t x, ivalue_t a, ivalue_t b)
{
    if (x < a) return a;
    if (x > b) return b;
    return x;
}

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

// Built-in function implementations - args are pre-evaluated
static value_t fn_min(csp_rt_t* st,uint16_t type,value_t* args,uint8_t nargs)
{
    value_t ret;
    (void)st; (void)nargs;
    ret.i = imin(args[0].i, args[1].i);
    return ret;
}

static value_t fn_max(csp_rt_t* st,uint16_t type,value_t* args,uint8_t nargs)
{
    value_t ret;    
    (void)st; (void)nargs;
    ret.i = imax(args[0].i, args[1].i); 
    return ret;
}

static value_t fn_abs(csp_rt_t* st,uint16_t type,value_t* args,uint8_t nargs)
{
    value_t ret;    
    (void)st; (void)nargs;
    ret.i = iabs(args[0].i);
    return ret;
}

static value_t fn_fabs(csp_rt_t* st,uint16_t type,value_t* args,uint8_t nargs)
{
    value_t ret;
    float arg = args[0].f;
    (void)st; (void)nargs;
    ret.f = (arg < 0) ? -arg : arg;
    return ret;
}

static value_t fn_clip(csp_rt_t* st,uint16_t type,value_t* args, uint8_t nargs)
{
    value_t ret;    
    (void)st; (void)nargs;
    ret.i = iclip(args[0].i, args[1].i, args[2].i);
    return ret;
}

static value_t fn_sign(csp_rt_t* st,uint16_t type, value_t* args, uint8_t nargs)
{
    value_t ret;
    (void)st; (void)nargs;
    ret.i = isign(args[0].i);
    return ret;
}

static value_t fn_timeout(csp_rt_t* st,uint16_t type,
			  value_t* args, uint8_t nargs)
{
    value_t ret;
    index_t ty = args[0].u;
    ret.i = BOOL(!st->decl[INDEX(ty)].tm.running);
    return ret;
}

// FIXME: mark the type info is needed?
static value_t fn_print(csp_rt_t* st, uint16_t type,
			 value_t* args,uint8_t nargs)
{
    value_t ret;
    ret.i = csp_print_value(st, type & 0xf, args[0]);
    return ret;
}

static value_t fn_tick(csp_rt_t* st,uint16_t type,value_t* args,uint8_t nargs)
{
    value_t ret;
    (void)st; (void)args; (void)nargs;
    ret.i = csp_time_ms();
    return ret;
}

static value_t fn_cycle(csp_rt_t* st,uint16_t type,value_t* args, uint8_t nargs)
{
    value_t ret;    
    (void)args; (void)nargs;
    ret.i = st->cycle;
    return ret;
}

// Built-in function table
// { name, namelen, nargs, rtype, {argtypes}, fn }
const csp_func_t csp_builtin_funcs[] = {
    { "",        0, 0, V_VOID,    {0,0,0,0},                         NULL },
    { "min",     3, 2, V_INTEGER, {V_INTEGER,V_INTEGER,0,0},      fn_min },
    { "max",     3, 2, V_INTEGER, {V_INTEGER,V_INTEGER,0,0},      fn_max },
    { "abs",     3, 1, V_INTEGER, {V_INTEGER,0,0,0},              fn_abs },
    { "fabs",    4, 1, V_FLOAT,   {V_FLOAT,0,0,0},                fn_fabs },
    { "sign",    4, 1, V_INTEGER, {V_INTEGER,0,0,0},              fn_sign },
    { "clip",    4, 3, V_INTEGER, {V_INTEGER,V_INTEGER,V_INTEGER,0}, fn_clip},
    { "timeout", 7, 1, V_INTEGER, {V_INDEX,0,0,0},                fn_timeout },
    { "print",   5, 1, V_INTEGER, {V_ANY,0,0,0},                  fn_print },
    { "tick",    4, 0, V_INTEGER, {0,0,0,0},                      fn_tick },
    { "cycle",   5, 0, V_INTEGER, {0,0,0,0},                      fn_cycle },
};

const uint8_t csp_num_builtin_funcs = sizeof(csp_builtin_funcs)/sizeof(csp_builtin_funcs[0]);

// Lookup function by name - returns builtin index (positive) or user index (negative-1)
// Returns 0 if not found
int csp_lookup_func(csp_rt_t* st, const char* name, uint8_t namelen)
{
    int i;
    // Check user functions first
    if (st->ufuncs) {
	for (i = 0; i < st->num_ufuncs; i++) {
	    if (st->ufuncs[i].namelen == namelen &&
		memcmp(st->ufuncs[i].name, name, namelen) == 0) {
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



int find_op_entry(char* name, int name_len)
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

static inline int arity(tok_t t)
{
    return op_table[t].arity;
}

static inline int prec(tok_t t)
{
    return op_table[t].prec;
}

static inline int assoc(tok_t t)
{
    return op_table[t].assoc;
}

// enq all rules that depend on declaration x
void csp_enq_elist(csp_rt_t* st, index_t x)
{
#if defined(SUPPORT_REACTIVE) && (SUPPORT_REACTIVE==1)    
    int i;
    index_t ix = INDEX(x);
    index_t base = st->ofs[ix];
    for (i = 0; i < st->idg[ix]; i++) {
	index_t p = st->edg[base+i];  // parent node
#ifdef DEBUG	
	printf("enq: %d\n", p);
#endif
	csp_enq(st, p);
    }
#endif
}

// set value on declaration node (variable/digital/analog ...)
void csp_set_value(csp_rt_t* st, index_t n, value_t v)
{
    int i = st_index(st, n);
    value_t cv = st->dout[i];
    if (v.u != cv.u) {
	bitset_set(st->dset, i);
	st->anyd = CSP_TRUE;
#if defined(SUPPORT_REACTIVE) && (SUPPORT_REACTIVE==1)
	if (st->reactive)
	    csp_enq_elist(st,n);
#endif
	st->dout[i] = v;
	st->update++;
    }    
}

void csp_set_ivalue(csp_rt_t* st, index_t n, ivalue_t v)
{
    value_t vv;
    vv.i = v;
    csp_set_value(st, n, vv);
}

void csp_set_fvalue(csp_rt_t* st, index_t n, fvalue_t v)
{
    value_t vv;
    vv.f = v;
    csp_set_value(st, n, vv);
}

// eval until NEXT!
int csp_eval_rule(csp_rt_t* st, int n)
{
    opcode_t op;
#if defined(SUPPORT_STATISTICS) && (SUPPORT_STATISTICS==1)
    st->num_eval_rule++;
#endif
#ifdef DEBUG
    printf("eval_rule: %d\n", n);
    // csp_dump_instr(stdout, 0, st, n, ".");
#endif

again:
#if defined(SUPPORT_STATISTICS) && (SUPPORT_STATISTICS==1)    
    st->num_eval0++;
#endif    
    op = st->instr[n].op;
    switch(op) {
    case OP_NOP:
	break;
    case OP_LD:
	st->reg[st->instr[n].m.x] = csp_value(st, st->instr[n].m.mem);
#ifdef DEBUG_XYZ	
	printf("%d: ld r%d ", n, st->instr[n].m.x);
	csp_fprint_tag(stdout, st, st->instr[n].m.mem);
	printf("\n");
	printf("%d: r%d=%d\n", n, st->instr[n].m.x,
	       st->reg[st->instr[n].m.x].i);
#endif
	break;
    case OP_ST:	
	csp_set_value(st, st->instr[n].m.mem, st->reg[st->instr[n].m.x]);
#ifdef DEBUG_XYZ
	printf("%d: st r%d ", n, st->instr[n].m.x);
	csp_fprint_tag(stdout, st, st->instr[n].m.mem);
	printf("\n");
	printf("%d: r%d=%d\n", n, st->instr[n].m.x,
	       st->reg[st->instr[n].m.x].i);
#endif
	break;	
    case OP_LI:
	st->reg[st->instr[n].i.x].i = st->instr[n].i.imm;
	break;
    case OP_ARG:
	st->arg[st->instr[n].i.imm] = st->reg[st->instr[n].i.x];
	break;
    case OP_RULE:
	if (st->reg[st->instr[n].r.cnd].i)
	    n = n+1;
	else
	    n = st->instr[n].r.nxt;
	goto again;
    case OP_NEXT: // rule is done executing
	return n+1;
    case OP_ENTER: // skip y + 2
	return n + st->instr[n].e.num + 2;
    case OP_NEW:
	if (!st->reactive) {
	    index_t ent = st->instr[n].n.ent;;
	    index_t obj = st->instr[n].n.obj;
	    // in non-reactive mode this is like a call
	    st->stack[st->esp].ix = n+1;      // return address
	    st->stack[st->esp].cur = st->cur;  // store current module
	    st->esp++;
	    // setup locals
	    st->cur = st->decl[INDEX(obj)].mq.m;    // set current module
	    st->offs[CURRENT] = st->offs[st->cur];  // setyp locals
	    return INDEX(ent)+1; // first instruction
	}
	break;
    case OP_LEAVE:
	if (!st->reactive) {
	    // return in non-reactive-mode
	    if (st->esp == 0)
		return st->ps.nn; // make it stop
	    st->esp--;
	    // restore locals
	    st->cur = st->stack[st->esp].cur;
	    n = st->stack[st->esp].ix;
	    st->offs[CURRENT] = st->offs[st->cur];
	    return n;
	}
	break;
    case OP_CALL: {
	// y: function index (low bit: 0=builtin, 1=user), index >> 1
	// z: argument (0/1 arg) or OP_COMMA instruction (2+ args)
	index_t idx = st->instr[n].f.idx;
	const csp_func_t* func = NULL;
	
	// Get function pointer
	if (st->instr[n].f.usr) {
	    if (st->ufuncs && (idx < st->num_ufuncs))
		func = &st->ufuncs[idx];
	}
	else {
	    if (idx < csp_num_builtin_funcs)
		func = &csp_builtin_funcs[idx];
	}
	if (func && func->fn) {
	    value_t val = func->fn(st, st->instr[n].f.avt,
				    st->arg, func->nargs);
	    st->reg[st->instr[n].f.x] = val;
	}
	break;
    }
    default: {
	value_t xv, yv, zv;
	
	switch(info_tab[op].arity) {
	case 0:
	    xv = eval_tab0[op]();
	    break;
	case 1:
	    yv = st->reg[st->instr[n].a.y];
	    xv = eval_tab1[op](yv);
#ifdef DEBUG_XYZ
	    printf("%d: r%d = %s r%d\n",
		   n, st->instr[n].a.x, csp_op_name(op),
		   st->instr[n].a.y);
	    printf("%d: r%d=%d, r%d=%d\n",
		   n,
		   st->instr[n].a.x, xv.i,
		   st->instr[n].a.y, yv.i);
#endif
	    break;
	case 2:
	    yv = st->reg[st->instr[n].a.y];
	    zv = st->reg[st->instr[n].a.z];
	    xv = eval_tab2[op](yv,zv);
#ifdef DEBUG_XYZ
	    printf("%d: r%d = r%d %s r%d\n",
		   n, st->instr[n].a.x, st->instr[n].a.y,
		   csp_op_name(op), 
		   st->instr[n].a.z);	
	    printf("%d: r%d=%d, r%d=%d, r%d=%d\n",
		   n,
		   st->instr[n].a.x, xv.i,	       
		   st->instr[n].a.y, yv.i,
		   st->instr[n].a.z, zv.i);
#endif
	    break;	    
	}
	st->reg[st->instr[n].a.x] = xv;
	break;
    }
    }
    n = n+1;
    goto again;
}

// undo all values
void csp_undo(csp_rt_t* st)
{
    st->anyd = CSP_FALSE;
    bitset_zero(st->dset);
}

void csp_commit(csp_rt_t* st)
{
#if defined(SUPPORT_TRANSACTION) && (SUPPORT_TRANSACTION==1)
    // swap in / out
    if (st->transaction) {
	value_t* tmp;
	// printf("swap transaction input/output\n");
	// tmp = st->xin; st->xin = st->xout; st->xout = tmp;
	tmp = st->din; st->din = st->dout; st->dout = tmp;
    }
#endif
    bitset_zero(st->dset);
    st->anyd = CSP_FALSE;   
}

// run eval_rule on all nodes

index_t csp_eval(csp_rt_t* st)
{
    index_t n = 0;
    index_t x = BAD_INDEX;
    st->cycle++;
    while(n < st->ps.nn) {
	n = csp_eval_rule(st, n);
	x = n;
    }
    return x;
}

// run queue until cycle boundary
index_t csp_react(csp_rt_t* st)
{
    st->cycle++;
#if defined(SUPPORT_REACTIVE) && (SUPPORT_REACTIVE==1)
    index_t x0, x1 = BAD_INDEX;
    if (st->reactive) {
	int cycle_end = st->tl;  // items added during cycle go to next cycle
	memset(st->inq, 0, sizeof(st->inq));  // allow rules to be queued again
	while(st->hd < cycle_end) {
	    x0 = csp_deq(st);
	    csp_eval_rule(st, x0);
	    x1 = x0;
	}
    }
    return x1;
#else
    return BAD_INDEX;
#endif
}

// look for symbol among nodes in range [start, stop)
// fixme: skip DECL_MODULE when scanning for names?
//  otherwise we may endup using module local variables
//
static index_t lookup_decl_in(csp_rt_t* st, char* name, int name_len,
			      int start, int stop)
{
    int i = start;

    while(i < stop) {
	int pos = st->decl[i].name;
	if (pos > 0) {
	    int len = st->str[pos-1];
	    index_t ix = MAKE_INDEX(0,i);
	    if ((len == name_len) &&
		(memcmp(decl_name(st, ix),name, name_len)==0)) {
		return ix;
	    }
	}
	if (st->decl[i].type == DECL_MODULE) // skip module def
	    i += (st->decl[i].md.n+1); // skip elements and END
	i++;
    }
    return BAD_INDEX;
}

static index_t lookup_decl(csp_rt_t* st, char* name, int name_len)
{
    return lookup_decl_in(st, name, name_len, INDEX(st->mdef)+1, st->ps.nd);
}

// lookup names like Mod.var
index_t csp_lookup_decl(csp_rt_t* st, char* module, char* name)
{
    if (module == NULL) {
	return lookup_decl(st, name, strlen(name));
    }
    else {
	index_t mx = lookup_decl(st, module, strlen(module));
	ivalue_t dn;
	if (mx == BAD_INDEX)
	    return BAD_INDEX;
	dn = st->decl[mx].md.n;  // number of elements
	return lookup_decl_in(st,name,strlen(name),
			      INDEX(mx)+1,INDEX(mx)+1+dn);
    }
}

index_t lookup_const(csp_rt_t* st, vtype_t vt, value_t v)
{
    index_t i;
    for (i = 0; i < st->ps.nd; i++) {
	if (IS_CONST(st, i) && (vt == st->decl[i].vt)) {
	    if (st->decl[i].cn.init.u == v.u)  // binary compare!
		return MAKE_INDEX(0,i);
	}
    }
    return BAD_INDEX;
}

index_t lookup_string_const(csp_rt_t* st, char* str, int len)
{
    index_t i;
    for (i = 0; i < st->ps.nd; i++) {
	if (IS_CONST(st, i) && (st->decl[i].vt == V_STRING)) {
	    sindex_t si = st->decl[i].cn.init.s;
	    int sn = st->str[si-1];  // length is in byte before spos
	    if ((sn == len) &&
		(strcmp(str, &st->str[st->decl[i].cn.init.s]) == 0))
		return MAKE_INDEX(0,i);		
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
    sindex_t pos = st->ps.strp;
    sindex_t next = pos + (len+2);
    if (next >= MAX_STR_BUF) {
	csp_set_error(st, ERR_STRING_SPACE_EXHUSTED);
	return -1;
    }
    st->ps.strp = next;  // allocate
    st->str[pos] = len;
    memcpy(&st->str[pos+1],name,len);
    st->str[pos+1+len] = '\0';
    return pos+1;
}

index_t next_decl_index(csp_rt_t* st)
{
    index_t ix;
    if (st->ps.nd >= MAX_DECLS) {
	csp_set_error(st, ERR_TOO_MANY_DECLARATIONS);
	return BAD_INDEX;
    }
    ix = MAKE_INDEX(0, st->ps.nd);
    st->ps.nd++;
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
    st->din[i].i = v;
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
    st->din[i].f = v;
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
    st->din[i].s = pos;    
    return ix;
}

int csp_new_instr(csp_rt_t* st, opcode_t op)
{
    int i;
    if ((i = st->ps.nn) >= MAX_INSTRS) {
	csp_set_error(st, ERR_TOO_MANY_INSTRUCTIONS);
	return -1;
    }
    st->ps.nn++;
    st->instr[i].op = op;
    return i;
}

int csp_new_nop(csp_rt_t* st)
{
    return csp_new_instr(st, OP_NOP);
}

int csp_new_next(csp_rt_t* st)
{
    return csp_new_instr(st, OP_NEXT);
}

int csp_new_mem(csp_rt_t* st, opcode_t op, reg_t x, index_t mem)
{
    int i;
    if ((i = csp_new_instr(st, op)) >= 0) {
	st->instr[i].m.x = x;
	st->instr[i].m.mem = mem;
    }
    return i;
}

int csp_new_st(csp_rt_t* st, reg_t x, index_t mem)
{
    return csp_new_mem(st, OP_ST, x, mem);
}

int csp_new_ld(csp_rt_t* st, reg_t x, index_t mem)
{
    return csp_new_mem(st, OP_LD, x, mem);
}

int csp_new_imm(csp_rt_t* st, opcode_t op, reg_t x, int16_t imm)
{
    int i;
    if ((i = csp_new_instr(st, op)) >= 0) {
	st->instr[i].i.x = x;
	st->instr[i].i.imm = imm;
    }
    return i;
}

int csp_new_li(csp_rt_t* st, reg_t x, int16_t imm)
{
    return csp_new_imm(st, OP_LI, x, imm);
}

int csp_new_arg(csp_rt_t* st, reg_t x, int16_t i)
{
    return csp_new_imm(st, OP_ARG, x, i);
}

int csp_new_alu(csp_rt_t* st, opcode_t op,reg_t x, reg_t y, reg_t z)
{
    int i;
    if ((i = csp_new_instr(st, op)) >= 0) {
	st->instr[i].a.x = x;
	st->instr[i].a.y = y;
	st->instr[i].a.z = z;
    }
    return i;
}

int csp_new_rule(csp_rt_t* st, reg_t cnd, int nxt)
{
    int i;
    if ((i = csp_new_instr(st, OP_RULE)) >= 0) {
	st->instr[i].r.cnd = cnd;
	st->instr[i].r.nxt = nxt;
    }
    return i;
}

int csp_new_call(csp_rt_t* st, reg_t x, int func_idx, int is_user,
		 int n, uint16_t argcode)
{
    int i;
    if ((i = csp_new_instr(st, OP_CALL)) >= 0) {
	st->instr[i].f.x   = x;
	st->instr[i].f.idx = func_idx;
	st->instr[i].f.usr = is_user;
	st->instr[i].f.n   = n;
	st->instr[i].f.avt = argcode;
    }
    return i;    
}

int csp_new_enter(csp_rt_t* st, int n, index_t mx)
{
    int i;
    if ((i = csp_new_instr(st, OP_ENTER)) >= 0) {
	st->instr[i].e.num = n;
	st->instr[i].e.mx = mx;
    }
    return i;
}

int csp_new_leave(csp_rt_t* st, int n, index_t mx)
{
    int i;
    if ((i = csp_new_instr(st, OP_LEAVE)) >= 0) {
	st->instr[i].v.num = n;
	st->instr[i].v.mx = mx;
    }
    return i;
}

int csp_new_new(csp_rt_t* st, unsigned ent, index_t obj)
{
    int i;
    
    if ((i = csp_new_instr(st, OP_NEW)) >= 0) {
	st->instr[i].n.ent = ent;
	st->instr[i].n.obj = obj;
    }
    return i;
}

int new_expr2(csp_rt_t* st, opcode_t op, index_t x ,index_t y, index_t z)
{
    return csp_new_alu(st, op, x, y, z);
}

int new_expr1(csp_rt_t* st, opcode_t op, index_t x, index_t y)
{
    return csp_new_alu(st, op, x, y, ZERO);
}

int new_expr0(csp_rt_t* st, opcode_t op)
{
    return csp_new_alu(st, op, ZERO, ZERO, ZERO);
}

// Build reactive dependency graph: declaration -> rules that depend on it
// When a declaration changes, we enqueue all rules that read from it (via LD)
void csp_csr(csp_rt_t* st)
{
#if defined(SUPPORT_REACTIVE) && (SUPPORT_REACTIVE==1)
    int i;
    int current_rule = -1;
    index_t wr[MAX_DECLS];

    // Clear in-degree counts
    memset(st->idg, 0, st->ps.nd * sizeof(index_t));

    // Pass 1: Count how many rules depend on each declaration
    // A rule depends on a declaration if it contains an LD from that declaration
    current_rule = 0;    
    for (i = 0; i < st->ps.nn; i++) {
	switch (st->instr[i].op) {
	case OP_RULE:
	    current_rule = -1;
	    break;
	case OP_NEXT:
	    current_rule = i+1;
	    break;
	case OP_LD:
	    // printf("ld rule=%d\n", current_rule);
	    if (current_rule >= 0) {
		index_t mem = INDEX(st->instr[i].m.mem);
		// printf("ld: mem=%d, nd=%d\n", mem, st->ps.nd);
		if (mem < st->ps.nd) {
		    st->idg[mem]++;
		    // printf("csr: idg[%d] = %d\n", mem, st->idg[mem]);
		}
	    }
	    break;
	case OP_ENTER:
	    current_rule = i+1;
	    break;
	case OP_LEAVE:
	    current_rule = -1;  // reset at module boundaries
	    break;
	default:
	    break;
	}
    }

    // Pass 2: Calculate offsets into edge array
    st->ofs[0] = 0;
    // printf("offs %d = %d\n", 0, st->ofs[0]);    
    for (i = 0; i < st->ps.nd; i++) {
	st->ofs[i+1] = st->ofs[i] + st->idg[i];
	// printf("offs %d = %d\n", i+1, st->ofs[i+1]);
    }

    // Pass 3: Fill in rule indices for each declaration
    memcpy(wr, st->ofs, st->ps.nd * sizeof(index_t));

    current_rule = 0;
    for (i = 0; i < st->ps.nn; i++) {
	switch (st->instr[i].op) {
	case OP_RULE:
	    current_rule = -1;
	    break;
	case OP_NEXT:
	    current_rule = i+1;
	    break;
	case OP_LD:
	    if (current_rule >= 0) {
		index_t mem = INDEX(st->instr[i].m.mem);
		if (mem < st->ps.nd)
		    st->edg[wr[mem]++] = current_rule;
	    }
	    break;
	case OP_ENTER:  // start of object
	    current_rule = i+1;
	    break;
	case OP_LEAVE:  // end of object
	    current_rule = -1;
	    break;
	default:
	    break;
	}
    }
#endif
}

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

#define TOK(x) do { tok = (x); goto done; } while(0)
#define SYM(x,p,l) do { tok = (x); val.str.ptr=(p); val.str.len=(l); goto done; } while(0)
#define TOK_INT(y) do { tok = INT; val.val.i = (y); goto done; } while(0)
#define TOK_FLT(y) do { tok = FLT; val.val.f = (y); goto done; } while(0)

int csp_next_token(char* str, token_t* tp)
{
    char* str0 = str;
    int c;
    int sign = 1;
    tok_t tok;
    tokval_t val;
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
    case '-':
	TOK(MINUS);
    case '+':
	TOK(PLUS);
    default:
	if (isdigit(c))
	    goto number;
	else if (isalpha(c)) {
	    char *name = str-1;
	    int len = 1;
	    int i;
	    while ( isalpha(*str) || isdigit(*str) || (*str == '_') ) {
		str++;
		len++;
	    }
	    if (len > MAX_NAME_LEN)
		return -1; // fixme set error code
	    if ((i = find_op_entry(name,len)) >= 0)
		TOK(op_table[i].tok);
	    SYM(WORD, name, len);
	}
	return -1;
    number:
	if ((c == '0') && (*str == 'x')) {
	    ivalue_t v = 0;
	    str++;
	    while(isxdigit(*str)) {
		v = v*16 + hex(*str++);
	    }
	    TOK_INT(v*sign);
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
		TOK_FLT(f*sign);
	    }
	    TOK_INT(v*sign);
	}
	return -1;
    }
done:
    tp->t = tok; tp->v = val;
    return str - str0;
}

// scan one line of tokens
int csp_scan_line(char* str, token_t* tv, size_t* num_toks)
{
    char* str0 = str;
    size_t i;
    size_t max_toks = *num_toks;

    i = 0;
    while(i < max_toks) {
	int n = csp_next_token(str, &tv[i]);
	if (n < 0)
	    return -1;
	str += n;
	if ((tv[i].t == NEWLINE) || (tv[i].t == NONE)) {
	    *num_toks = i;
	    return str-str0;
	}
	i++;
    }
    return -1;
}

void csp_pstate_save(csp_rt_t* st, csp_pstate_t* ps)
{
    *ps = st->ps;
}

void csp_pstate_restore(csp_rt_t* st, csp_pstate_t* ps)
{
    st->ps = *ps;
}


void alloc_init(reg_allocator_t* ap)
{
    int i;
    for (i = 0; i < MAX_REGS; i++) {
	ap->free_regs[i] = i;
	ap->rmap[i] = BAD_INDEX;
    }
    ap->top = 0;
}

int alloc_reg(csp_rt_t* st)
{
    reg_allocator_t* ap;    
    if ((ap = st->ap) != NULL) {
	int r = ap->free_regs[ap->top++];
	ap->rmap[r] = BAD_INDEX;
	return r;
    }
    return 0;
}

void free_reg(csp_rt_t* st, int r)
{
    reg_allocator_t* ap;
    if ((ap = st->ap) != NULL) {
	index_t ix;
	ap->free_regs[--ap->top] = r;
	if ((ix = ap->rmap[r]) != BAD_INDEX) {
	    ap->rmap[r] = BAD_INDEX;
	    st->decl[INDEX(ix)].is_mapped = 0;
	}
    }
}

// Map declaration (variable/constant/digital...)
int map_reg(csp_rt_t* st, index_t ix)
{
    reg_allocator_t* ap;
    int dst;
    
    if ((ap = st->ap) != NULL) {
	// Check if already mapped AND mapping is still valid
	if (st->decl[INDEX(ix)].is_mapped) {
	    reg_t r = st->decl[INDEX(ix)].reg;
	    if (st->ap->rmap[r] == ix)
		return r;  // mapping still valid
	    // Stale mapping - clear it
	    st->decl[INDEX(ix)].is_mapped = 0;
	}
	dst = alloc_reg(st);
	st->decl[INDEX(ix)].is_mapped = 1;
	st->decl[INDEX(ix)].reg = dst;
	ap->rmap[dst] = ix;
	if (st->decl[INDEX(ix)].type == DECL_CONSTANT) {
	    if (st->decl[INDEX(ix)].vt == V_INTEGER) {
		value_t val = st->decl[INDEX(ix)].cn.init;
		if ((val.i >= -32768) && (val.i <= 32767))
		    if (csp_new_li(st,dst,val.i) < 0)
			return -1;
		return dst;
	    }
	    if (st->decl[INDEX(ix)].vt == V_UNSIGNED) {
		value_t val = st->decl[INDEX(ix)].cn.init;
		if (val.u <= 32767)
		    if (csp_new_li(st,dst,val.u) < 0)
			return -1;
		return dst;
	    }
	}
	// generate LD instruction
	if (csp_new_ld(st,dst,ix) < 0)
	    return -1;
	return dst;
    }
    return 0;
}

// Parser stack entry - tracks both register and declaration index
typedef struct {
    reg_t reg;       // register number (valid if loaded)
    index_t ix;      // declaration index (valid for variables)
    value_t val;     // if constant then the actual value is loaded here
    unsigned loaded:1;  // 1 if value is loaded in reg
    unsigned immediate:1; // 1 if val is set or calculated
    unsigned vt:TYPE_BITS;  // value type (vtype_t)
} rstack_entry_t;

// Push immediate value (integer, float, or string constant)
static int push_imm(csp_rt_t* st, rstack_entry_t* rstack, int* ep,
		    vtype_t vt, value_t val)
{
    reg_t r = 0;
    index_t ix = BAD_INDEX;

    if (st->ap) {
	if (vt == V_INTEGER && val.i >= -32768 && val.i <= 32767) {
	    r = alloc_reg(st);
	    if (csp_new_li(st, r, val.i) < 0) return -1;
	} else {
	    ix = lookup_const(st, vt, val);
	    if (ix == BAD_INDEX) {
		if (vt == V_FLOAT) ix = new_float_const(st, val.f);
		else ix = new_signed_const(st, val.i);
	    }
	    if (ix == BAD_INDEX) return -1;
	    r = map_reg(st, ix);
	    if (r < 0) return -1;
	}
    }
    rstack[(*ep)++] = (rstack_entry_t){
	.reg = r, .ix = ix, .val = val,
	.loaded = (st->ap != NULL), .immediate = 1, .vt = vt
    };
    return 0;
}

// Push string constant
static int push_str(csp_rt_t* st, rstack_entry_t* rstack, int* ep,
		    char* ptr, int len)
{
    reg_t r = 0;
    index_t ix;

    ix = lookup_string_const(st, ptr, len);
    if (ix == BAD_INDEX) ix = new_string_const(st, ptr, len);
    if (ix == BAD_INDEX) return -1;

    if (st->ap) {
	r = map_reg(st, ix);
	if (r < 0) return -1;
    }
    rstack[(*ep)++] = (rstack_entry_t){
	.reg = r, .ix = ix,
	.loaded = (st->ap != NULL), .immediate = 0, .vt = V_STRING
    };
    return 0;
}

// Push variable/declaration reference
static int push_var(csp_rt_t* st, rstack_entry_t* rstack, int* ep,
		    index_t ix, vtype_t vt)
{
    reg_t r = 0;

    if (st->ap) {
	r = map_reg(st, ix);
	if (r < 0) return -1;
    }
    rstack[(*ep)++] = (rstack_entry_t){
	.reg = r, .ix = ix,
	.loaded = (st->ap != NULL), .immediate = 0, .vt = vt
    };
    return 0;
}

// Push L-value (assignment target, index only, no load)
static inline void push_lval(rstack_entry_t* rstack, int* ep, index_t ix, vtype_t vt)
{
    rstack[(*ep)++] = (rstack_entry_t){
	.reg = 0, .ix = ix,
	.loaded = 0, .immediate = 0, .vt = vt
    };
}

// Push register result (from operation)
static inline void push_reg(rstack_entry_t* rstack, int* ep, reg_t r, vtype_t vt)
{
    rstack[(*ep)++] = (rstack_entry_t){
	.reg = r, .ix = BAD_INDEX,
	.loaded = 1, .immediate = 0, .vt = vt
    };
}

// Push folded constant result (no register)
static inline void push_folded(rstack_entry_t* rstack, int* ep, value_t val, vtype_t vt)
{
    rstack[(*ep)++] = (rstack_entry_t){
	.reg = 0, .ix = BAD_INDEX, .val = val,
	.loaded = 0, .immediate = 1, .vt = vt
    };
}

// Process binary assignment operator: generates ST instruction
// Returns new ep on success, -1 on error
static int process_assign(csp_rt_t* st, rstack_entry_t* rstack, int ep)
{
    rstack_entry_t lhs = rstack[ep-2];
    rstack_entry_t rhs = rstack[ep-1];
    vtype_t ltype;

    if (lhs.ix == BAD_INDEX) {
	csp_set_error(st, ERR_SYNTAX);  // left side must be l-value
	return -1;
    }

    // Get target type from declaration
    ltype = st->decl[INDEX(lhs.ix)].vt;

    // Type conversion if needed
    if (ltype == V_INTEGER && rhs.vt == V_FLOAT) {
	// float→int
	if (rhs.immediate) {
	    rhs.val.i = (ivalue_t)rhs.val.f;
	    rhs.vt = V_INTEGER;
	}
	if (rhs.loaded && st->ap) {
	    reg_t r = alloc_reg(st);
	    if (new_expr1(st, OP_CVTFI, r, rhs.reg) < 0)
		return -1;
	    free_reg(st, rhs.reg);
	    rhs.reg = r;
	    rhs.vt = V_INTEGER;
	}
    } else if (ltype == V_FLOAT && rhs.vt == V_INTEGER) {
	// int→float
	if (rhs.immediate) {
	    rhs.val.f = (fvalue_t)rhs.val.i;
	    rhs.vt = V_FLOAT;
	}
	if (rhs.loaded && st->ap) {
	    reg_t r = alloc_reg(st);
	    if (new_expr1(st, OP_CVTIF, r, rhs.reg) < 0)
		return -1;
	    free_reg(st, rhs.reg);
	    rhs.reg = r;
	    rhs.vt = V_FLOAT;
	}
    }

    // If rhs is a folded constant, load it into a register
    if (!rhs.loaded && rhs.immediate) {
	if (rhs.vt == V_FLOAT) {
	    // Create float constant and load it
	    index_t ix = lookup_const(st, V_FLOAT, rhs.val);
	    if (ix == BAD_INDEX)
		ix = new_float_const(st, rhs.val.f);
	    if (ix == BAD_INDEX) return -1;
	    rhs.reg = map_reg(st, ix);
	    if (rhs.reg < 0) return -1;
	} else {
	    reg_t r = alloc_reg(st);
	    if (csp_new_li(st, r, rhs.val.i) < 0)
		return -1;
	    rhs.reg = r;
	}
	rhs.loaded = 1;
    }
    if (!rhs.loaded) {
	csp_set_error(st, ERR_SYNTAX);  // rhs must have value
	return -1;
    }
    // Generate store instruction
    if (csp_new_st(st, rhs.reg, lhs.ix) < 0)
	return -1;
    // Result is the rhs (for chaining A=B=1)
    rstack[ep-2] = rhs;
    return ep - 1;
}

// Get float version of arithmetic opcode (or same if no float version)
static opcode_t float_op(opcode_t op)
{
    switch(op) {
    case OP_ADD: return OP_FADD;
    case OP_SUB: return OP_FSUB;
    case OP_MUL: return OP_FMUL;
    case OP_DIV: return OP_FDIV;
    case OP_NEG: return OP_FNEG;
    case OP_LT: return OP_FLT;
    case OP_LTE: return OP_FLTE;
    case OP_GT: return OP_FGT;
    case OP_GTE: return OP_FGTE;
    case OP_EQEQ: return OP_FEQEQ;
    case OP_NEQ: return OP_FNEQ;
    default: return op;
    }
}

// Convert operand to float (int→float via cvtif)
static int coerce_to_float(csp_rt_t* st, rstack_entry_t* e)
{
    if (e->vt == V_FLOAT) return 0;  // already float
    if (e->vt != V_INTEGER) return -1;  // can only convert int

    if (e->immediate) {
	e->val.f = (float)e->val.i;
    }
    if (e->loaded && st->ap) {
	reg_t r = alloc_reg(st);
	if (new_expr1(st, OP_CVTIF, r, e->reg) < 0)
	    return -1;
	free_reg(st, e->reg);
	e->reg = r;
    }
    e->vt = V_FLOAT;
    return 0;
}

// Convert operand to int (float→int via cvtfi)
static int coerce_to_int(csp_rt_t* st, rstack_entry_t* e)
{
    if (e->vt == V_INTEGER) return 0;  // already int
    if (e->vt != V_FLOAT) return -1;  // can only convert float
    
    if (e->immediate) {
	e->val.i = (ivalue_t)e->val.f;
    }
    if (e->loaded && st->ap) {
	reg_t r = alloc_reg(st);
	if (new_expr1(st, OP_CVTFI, r, e->reg) < 0)
	    return -1;
	free_reg(st, e->reg);
	e->reg = r;
    }
    e->vt = V_INTEGER;
    return 0;
}


static int process_op(csp_rt_t* st, tok_t tok, rstack_entry_t* rstack, int ep)
{
    int dst;
    opcode_t op;
    vtype_t rt;

    switch(arity(tok)) {
    case 2: {
	rstack_entry_t* a = &rstack[ep-2];
	rstack_entry_t* b = &rstack[ep-1];

	switch(tok) {
	case EQ:
	    if ((ep = process_assign(st, rstack, ep)) < 0)
		return PARSE_ERROR;
	    break;
	case COMMA:
	    // comma: side effects already done via ST, just keep right operand
	    free_reg(st, a->reg);
	    *a = *b;
	    ep--;
	    break;
	default: {
	    vtype_t at = a->vt;
	    vtype_t bt = b->vt;

	    // Type coercion: promote to float if either operand is float
	    if (at == V_FLOAT || bt == V_FLOAT) {
		if (at == V_INTEGER && coerce_to_float(st, a) < 0)
		    return PARSE_ERROR;
		if (bt == V_INTEGER && coerce_to_float(st, b) < 0)
		    return PARSE_ERROR;
		op = float_op(op_table[tok].code);
	    } else {
		op = op_table[tok].code;
	    }
	    rt = info_tab[op].rtype;

	    if (a->immediate && b->immediate && eval_tab2[op]) {
		// constant fold
		value_t result = eval_tab2[op](a->val, b->val);
		free_reg(st, a->reg);
		free_reg(st, b->reg);
		*a = (rstack_entry_t){
		    .val = result, .ix = BAD_INDEX,
		    .loaded = 0, .immediate = 1, .vt = rt
		};
	    } else {
		// generate code
		dst = alloc_reg(st);
		if (new_expr2(st, op, dst, a->reg, b->reg) < 0)
		    return PARSE_ERROR;
		free_reg(st, a->reg);
		*a = (rstack_entry_t){
		    .reg = dst, .ix = BAD_INDEX,
		    .loaded = 1, .immediate = 0, .vt = rt
		};
	    }
	    ep--;
	}
	}
	break;
    }
    case 1: {
	rstack_entry_t* a = &rstack[ep-1];
	vtype_t at = a->vt;

	// Select float op if operand is float
	if (at == V_FLOAT) {
	    op = float_op(op_table[tok].code);
	} else {
	    op = op_table[tok].code;
	}
	rt = info_tab[op].rtype;

	if (a->immediate && eval_tab1[op]) {
	    // constant fold
	    value_t result = eval_tab1[op](a->val);
	    free_reg(st, a->reg);
	    *a = (rstack_entry_t){
		.val = result, .ix = BAD_INDEX,
		.loaded = 0, .immediate = 1, .vt = rt
	    };
	} else {
	    // generate code
	    dst = alloc_reg(st);
	    if (new_expr1(st, op, dst, a->reg) < 0)
		return PARSE_ERROR;
	    free_reg(st, a->reg);
	    *a = (rstack_entry_t){
		.reg = dst, .ix = BAD_INDEX,
		.loaded = 1, .immediate = 0, .vt = rt
	    };
	}
	break;
    }
    case 0:
	return PARSE_ERROR;
    }
    return ep;
}

void print_stack_used()
{
    // stack debug
    csp_print_str("StackUsed=");
    csp_print_int(stack_used());
    csp_println();
}


// num_toks is number of tokens and value on input
// num_toks is number of tokens consumed on output
int csp_parse_expr(csp_rt_t* st,token_t* tv,size_t* num_toks)
{
    tok_t op;
    tokval_t tval;
    tok_t pop = NONE;   // previous operator/token
    int pp = 0;         // operator stack pointer
    int ep = 0;         // expression stack pointer
    tok_t ostack[MAX_PARSE_STACK_DEPTH];  // stack of operators
    rstack_entry_t rstack[MAX_PARSE_STACK_DEPTH];  // stack of {reg, index}
    reg_t dst;
    index_t ix;
    int i = 0;
    size_t n = *num_toks;

next:
    if ((i >= n) || (tv[i].t==NEWLINE) || (tv[i].t==NONE))  // end-of-list
	goto out;
    op   = tv[i].t;
    tval = tv[i].v;
    i++;
    switch(op) {
    case QUEST: i--; goto out;
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
	    ((pop != INT) && (pop != WORD) && (pop != FLT)))
	    op = PLUS1;
	goto operator;
    case MINUS:
	if ((pop == NONE) || (pop == RP) ||
	    ((pop != INT) && (pop != WORD) && (pop != FLT)))
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
    case COMMA: goto operator;
    case LP:
	ostack[pp++] = LP; pop = LP; break;
    case RP:
	if (pp == 0)
	    return PARSE_ERROR;
	// Process operators until we hit LP or a function marker
	while(pp && ((op = ostack[pp-1]) != LP) && !IS_FUNC_MARKER(op)) {
	    // COMMA inside function call: just pop it, don't combine args
	    if (op == COMMA) {
		pp--;
		continue;
	    }
	    if ((ep = process_op(st, op, rstack, ep)) < 0)
		return PARSE_ERROR;
	    pp--;
	}
	if (pp && IS_FUNC_MARKER(ostack[pp-1])) {
	    // Function call - get func info and generate OP_CALL
	    // Note: LP was skipped in WORD case, so no LP to pop
	    tok_t marker = ostack[--pp];
	    int func_idx = FUNC_MARKER_INDEX(marker);
	    int is_user = FUNC_MARKER_IS_USER(marker);
	    int n, j;
	    const csp_func_t* func = NULL;
	    uint16_t argcode = 0;
	    
	    if (is_user) {
		if (st->ufuncs && (func_idx < st->num_ufuncs))
		    func = &st->ufuncs[func_idx];
	    } else {
		if (func_idx < csp_num_builtin_funcs)
		    func = &csp_builtin_funcs[func_idx];
	    }
	    if (!func || !func->fn)
		return PARSE_ERROR;
	    n = func->nargs;
	    for (j = 0; j < n; j++) {
		rstack_entry_t* arg = &rstack[ep-(n-j)];
		vtype_t argvt = arg->vt;
		vtype_t argtype = func->argtypes[j];

		argcode |= (argvt << 4*j);

		// check arguments & coerce
		switch(argtype) {
		case V_ANY:
		    break;  // OK
		case V_NUMBER:
		    if ((argvt != V_INTEGER) && (argvt != V_FLOAT))
			return PARSE_ERROR;
		    break;
		case V_INTEGER:
		    if (coerce_to_int(st, arg) < 0)
			return PARSE_ERROR;
		    break;
		case V_FLOAT:
		    if (coerce_to_float(st, arg) < 0)
			return PARSE_ERROR;
		    break;
		case V_STRING:
		    if (arg->vt != V_STRING)
			return PARSE_ERROR;
		    break;
		default:
		    if (argtype != argvt)
			return PARSE_ERROR;
		    break;
		}

		if (argtype == V_INDEX) {
		    reg_t r;		    
		    // Pass index directly: load index value into arg[]
		    if (arg->ix == BAD_INDEX) {
			csp_set_error(st, ERR_SYNTAX);
			return PARSE_ERROR;
		    }
		    r = alloc_reg(st);
		    if (csp_new_li(st, r, arg->ix) < 0)
			return PARSE_ERROR;
		    if (csp_new_arg(st, r, j) < 0)
			return PARSE_ERROR;
		    free_reg(st, r);
		    if (arg->loaded)
			free_reg(st, arg->reg);
		}
		else {
		    // Pass value: use loaded register
		    if (!arg->loaded) {
			csp_set_error(st, ERR_SYNTAX);
			return PARSE_ERROR;
		    }
		    if (csp_new_arg(st, arg->reg, j) < 0)
			return PARSE_ERROR;
		    free_reg(st, arg->reg);
		}
	    }
	    // pop rstack
	    if (n > 0) {
		ep -= n;
	    }
	    
	    dst = alloc_reg(st);
	    if (csp_new_call(st, dst, func_idx, is_user, n, argcode) < 0)
		return PARSE_ERROR;
	    push_reg(rstack, &ep, dst, func->rtype);
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
	if (push_imm(st, rstack, &ep, V_INTEGER, tval.val) < 0)
	    return PARSE_ERROR;
	pop = INT;
	break;
    case FLT:
	if (push_imm(st, rstack, &ep, V_FLOAT, tval.val) < 0)
	    return PARSE_ERROR;
	pop = FLT;
	break;
    case STR:
	if (push_str(st, rstack, &ep, tval.str.ptr, tval.str.len) < 0)
	    return PARSE_ERROR;
	pop = STR;
	break;
    case WORD: {
	// First check if this is a function call (WORD followed by LP)
	int func_res = csp_lookup_func(st, tval.str.ptr, tval.str.len);
	if (func_res != 0 && (tv[i].t == LP)) {
	    // It's a function call - push marker to ostack and skip LP
	    // FIXME: check function after all arguments are parsed! ????
	    int is_user = (func_res < 0) ? 1 : 0;
	    int func_idx = is_user ? (-func_res - 1) : func_res;
	    ostack[pp++] = MAKE_FUNC_MARKER(func_idx, is_user);
	    i++;  // skip the LP token
	    pop = LP;
	}
	else {
	    // Not a function - regular variable/decl lookup
	    if ((ix = lookup_decl(st,tval.str.ptr,tval.str.len)) == BAD_INDEX) {
		csp_set_error(st, ERR_VARIABLE_NOT_DECLARED);
		return PARSE_ERROR;
	    }
	    // Handle obj.field access
	    if ((st->decl[INDEX(ix)].type == DECL_OBJECT) &&
		(tv[i].t == DOT) && (tv[i+1].t == WORD)) {
		index_t mx = st->decl[INDEX(ix)].mq.mx;  // module def
		ivalue_t dn = st->decl[INDEX(mx)].md.n;  // number of elements
		index_t jx;
		tval = tv[i+1].v;
		if ((jx = lookup_decl_in(st,tval.str.ptr,tval.str.len,
					 INDEX(mx)+1,INDEX(mx)+1+dn)) == BAD_INDEX) {
		    csp_set_error(st, ERR_OBJECT_NOT_DEFINED);
		    return PARSE_ERROR;
		}
		ix = MAKE_INDEX(st->decl[INDEX(ix)].mq.m,INDEX(jx));
		i += 2;
	    }
	    // Apply module context
	    if ((st->mdef != 0) && (OBJ(ix) == 0))
		ix = MAKE_INDEX(CURRENT, INDEX(ix));

	    // Check if this is an l-value (assignment target)
	    vtype_t vt = st->decl[INDEX(ix)].vt;
	    if (tv[i].t == EQ) {
		// L-value: push index only, no load
		push_lval(rstack, &ep, ix, vt);
	    }
	    else {
		// R-value: load into register
		if (push_var(st, rstack, &ep, ix, vt) < 0)
		    return PARSE_ERROR;
	    }
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
	    int p2;
	    // FUNC_MARKER acts like LP - don't process operators past it
	    if (IS_FUNC_MARKER(op2) || op2 == LP) {
		ostack[pp++] = op;
		pop = op;
		goto next;
	    }
	    p2 = prec(op2);

	    while ( ((p2 > p1) && (op2 != LP)) ||
		    ((p2 == p1) && (assoc(op2) < 0))) {
		if ((ep = process_op(st, op2, rstack, ep)) < 0)
		    return PARSE_ERROR;
		pp--;
		if (pp == 0) break;
		op2 = ostack[pp-1];
		if (IS_FUNC_MARKER(op2) || op2 == LP) break;
		p2 = prec(op2);
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

	if ((ep = process_op(st, op, rstack, ep)) < 0)
	    return PARSE_ERROR;
    }
    if (pp < 0)
	return PARSE_ERROR;
    if (ep == 1) {
	*num_toks = i;
	return rstack[0].reg;
    }
    return PARSE_ERROR;
}

// parse constant (fixme eval constants?)
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

// parse constant
// from a constant declaration
// from a user constant
// or integer value
//
static int expect_const(csp_rt_t* st, token_t* tv, int i,
			token_t* rp, int num)
{
    if (num < 1)
	return -1;
    if (tv[i].t == INT) {
	*rp = tv[i];
	return i+1;
    }
    else if (tv[i].t == WORD) {
	index_t ix;
	char* name = tv[i].v.str.ptr;
	int name_len = tv[i].v.str.len;
	
	if ((ix = lookup_decl(st,name,name_len)) != BAD_INDEX) {
	    if ((st->decl[ix].type != DECL_CONSTANT) &&
		(st->decl[ix].vt != V_INTEGER))
		return -1;
	    rp->t = INT;
	    rp->v.val.i = st->decl[ix].cn.init.i;
	    return i+1;
	}
	if (st->uconst == NULL)
	    return -1;
	if (st->uconst(st,tv[i].v.str.ptr,tv[i].v.str.len,&rp->v.val.i)) {
	    // only integer constants for now
	    rp->t = INT;
	    return i+1;
	}
	return -1;
    }
    return -1;
}

// match a single INT token
// or an simple expression like ['-' INT] or ['+' INT]
// return next index (i+1) on succes
// and -1 on failure

static int expect_integer(csp_rt_t* st, token_t* tv, int i,
			  token_t* rp, int num)
{
    if ((tv[i].t == MINUS) && (num >= 2)) {
	if ((i = expect_const(st, tv, i+1, rp, num-1)) < 0)
	    return -1;
	rp->v.val.i = -rp->v.val.i; // negate
	return i;
    }
    else if ((tv[i].t == PLUS) && (num >= 2)) {
	if ((i = expect_const(st, tv, i+1, rp, num-1)) < 0)
	    return -1;
	return i;
    }
    return expect_const(st, tv, i, rp, num);
}

static int expect(token_t* tv, int i, tok_t* te)
{
    int j = 0;
    int t;

    do {
	t = te[j];
	if ((t != LAST) && (t != (int)tv[i].t))
	    return 0;
	j++;
	i++;
    } while(t != LAST);
    return 1;
}


// '#' 'module' <name>
int csp_parse_module(csp_rt_t* st, token_t* tv, size_t n)
{
    index_t ix;
    index_t jx;
    tok_t te[] = {HASH, MODULE, WORD, LAST};

    if (!expect(tv, 0, te)) {
	csp_set_error(st, ERR_SYNTAX);
	return -1;
    }
    if ((ix = lookup_decl(st, tv[2].v.str.ptr, tv[2].v.str.len)) != BAD_INDEX) {
	csp_set_error(st, ERR_OBJECT_ALREADY_DEFINED);
	return -1;
    }
    ix = csp_new_decl(st, tv[2].v.str.ptr, tv[2].v.str.len, DECL_MODULE);
    if (ix == BAD_INDEX) return -1;
    st->mdef = ix;  // current module being defined
    if ((jx = csp_new_enter(st, 0, ix)) < 0)
	return -1;
    st->ent = jx;   // entry point of module being defined
    st->decl[INDEX(ix)].md.n = 0;
    st->decl[INDEX(ix)].md.ent = st->ent;
    return 0;
}

// '#' 'end' [....]
int csp_parse_end(csp_rt_t* st, token_t* tv, size_t n)
{
    index_t mx, ex, lx;
    tok_t te[] = {HASH, END, LAST};    

    if (!expect(tv, 0, te)) {
	csp_set_error(st, ERR_SYNTAX);	
	return -1;
    }
    if ((mx = st->mdef)) { // stack?
	if ((ex = csp_new_decl(st, NULL, 0, DECL_END)) == BAD_INDEX)
	    return -1;
	st->decl[INDEX(mx)].md.n = (INDEX(ex) - INDEX(mx)) - 1;
	if ((lx = csp_new_leave(st, 0, 0)) < 0)
	    return -1;
	// ent MUST be OP_ENTER!
	st->instr[st->ent].e.num = (lx - st->ent - 1);
	st->instr[lx].v.num = st->instr[st->ent].e.num;
	st->instr[lx].v.mx  = st->instr[st->ent].e.mx;
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
// Note that in is used for input arguments in objects
// and out is used for output argumets
//
int csp_parse_variable(csp_rt_t* st, token_t* tv, size_t n)
{
    ivalue_t res = MAKE_RES(8*sizeof(ivalue_t));
    ivalue_t in=0, out=0;
    vtype_t vt = V_INTEGER;
    value_t def;
    index_t ix;    
    int i;
    tok_t teres[] = {COLON, INT, LAST};    
    tok_t te[] = {HASH, VARIABLE, WORD, LAST};

    if (!expect(tv, 0, te)) {
	csp_set_error(st, ERR_SYNTAX);	
	return -1;
    }
    i=3;
    if (expect(tv, i, teres)) {
	res = MAKE_RES(tv[i+1].v.val.i);
	i += 2;
    }
    vt = V_INTEGER;
    def.i = 0;
    
opts:
    if (i < n) {
	switch(tv[i].t) {
	case UNSIGNED: vt=V_UNSIGNED; def.u = 0; i++; goto opts;
	case INTEGER: vt=V_INTEGER; def.i = 0; i++; goto opts;
	case FLOAT: vt=V_FLOAT; def.f = 0.0; i++;  goto opts;
	case IN: in = 1; i++; goto opts;
	case OUT: out = 1; i++; goto opts;
	case INOUT: in=out=1; i++; goto opts;
	default: break;
	}
    }
    if (tv[i].t == EQ) {
	if (parse_value(vt, tv[i+1].t, tv[i+1].v.val, &def) < 0) {
	    csp_set_error(st, ERR_SYNTAX);  // number expected
	    return -1;
	}
	i += 2;
    }
    if ((ix = lookup_decl(st, tv[2].v.str.ptr, tv[2].v.str.len)) == BAD_INDEX)
	ix = csp_new_decl(st, tv[2].v.str.ptr, tv[2].v.str.len, DECL_VARIABLE);
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
int csp_parse_constant(csp_rt_t* st, token_t* tv, size_t n)
{
    ivalue_t res;
    value_t cnst;
    vtype_t vt;
    index_t ix;  
    int i;
    tok_t teres[] = {COLON, INT, LAST};    
    tok_t te[] = {HASH, CONSTANT, WORD, LAST};
    
    // defaults
    vt = V_INTEGER;
    cnst.i = 0;
    res = MAKE_RES(8*sizeof(ivalue_t));

    if (!expect(tv, 0, te)) {
	csp_set_error(st, ERR_SYNTAX);	
	return -1;
    }
    i=3;
    if (expect(tv, i, teres)) {	
	res = MAKE_RES(tv[i+1].v.val.i);
	i += 2;
    }
    switch(tv[i].t) {
    case UNSIGNED: vt=V_UNSIGNED; cnst.u=0; i++; break;
    case INTEGER: vt=V_INTEGER; cnst.i=0; i++;  break;	    	    
    case FLOAT: vt=V_FLOAT; cnst.f=0.0; i++; break;
    default: break;
    }
    if (tv[i].t != EQ) {
	csp_set_error(st, ERR_SYNTAX);
	return -1;
    }
    if (parse_value(vt, tv[i+1].t, tv[i+1].v.val, &cnst) < 0) {
	csp_set_error(st, ERR_SYNTAX);	
	return -1;
    }
    i += 2;
    if ((ix = lookup_decl(st, tv[2].v.str.ptr, tv[2].v.str.len)) == BAD_INDEX)
	ix = csp_new_decl(st, tv[2].v.str.ptr, tv[2].v.str.len, DECL_CONSTANT);
    if (ix == BAD_INDEX) return -1;
    i = INDEX(ix);
    st->decl[i].res = res;
    st->decl[i].vt = vt;
    st->decl[i].cn.init = cnst;
    return 0;    
}

// '#' 'digital' <name> [<iodir>|<pull>] [<port>':']<pin>
int csp_parse_digital(csp_rt_t* st, token_t* tv, size_t n)
{
    ivalue_t res = MAKE_RES(1);
    ivalue_t in=0, out=0;
    ivalue_t pu=0, pd=0;
    ivalue_t port=0, pin=0;
    index_t ix;
    int i;
    tok_t te[] = {HASH, DIGITAL, WORD, LAST};
    tok_t teport[] = {INT, COLON, INT, LAST};        

    if (!expect(tv, 0, te)) {
	csp_set_error(st, ERR_SYNTAX);	
	return -1;
    }
    i = 3;
opts:
    if (i < n) {
	switch(tv[i].t) {
	case IN: in = 1; i++; goto opts;
	case OUT: out = 1; i++; goto opts;
	case INOUT: in=out=1; i++; goto opts;
	case PULLUP: pd=0; pu=1; i++; goto opts;
	case PULLDOWN: pu=0; pd=1; i++; goto opts;
	default: break;
	}
    }
    if (expect(tv, i, teport)) {
	port = tv[i].v.val.i;
	pin  = tv[i+2].v.val.i;
	i += 3;
    }
    else if (tv[i].t==INT) {
	pin = tv[i].v.val.i;
	i++;
    }
    else {
	csp_set_error(st, ERR_SYNTAX);	
	return -1;
    }
    if (!in && !out) in=1;
    if ((ix = lookup_decl(st, tv[2].v.str.ptr, tv[2].v.str.len)) == BAD_INDEX)
	ix = csp_new_decl(st, tv[2].v.str.ptr, tv[2].v.str.len, DECL_DIGITAL);
    if (ix == BAD_INDEX) return -1;
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
int csp_parse_analog(csp_rt_t* st, token_t* tv, size_t n)
{
    ivalue_t res;
    ivalue_t in=0, out=0;
    ivalue_t port=0, pin=0, pwm=0;
    vtype_t vt;
    index_t ix;
    int i;
    tok_t te[] = {HASH, ANALOG, WORD, LAST};
    tok_t teres[] = {COLON, INT, LAST};
    tok_t teport[] = {INT, COLON, INT, LAST};

    res = MAKE_RES(10);
    vt = V_INTEGER;    

    if (!expect(tv, 0, te)) {
	csp_set_error(st, ERR_SYNTAX);
	return -1;
    }
    i = 3;
    if (expect(tv, i, teres)) {
	res = MAKE_RES(tv[i+1].v.val.i);
	i += 2;
    }
opts:
    if (i < n) {
	switch(tv[i].t) {
	case UNSIGNED: vt=V_UNSIGNED; i++; goto opts;
	case INTEGER: vt=V_INTEGER; i++; goto opts;
	case FLOAT: vt=V_FLOAT; i++;  goto opts;
	case PWM:   pwm = 1; i++; goto opts;
	case IN:    in = 1; i++; goto opts;
	case OUT:   out = 1; i++; goto opts;
	case INOUT: in=out=1; i++; goto opts;
	default: break;
	}
    }
    if (expect(tv, i, teport)) {
	port = tv[i].v.val.i;
	pin  = tv[i+2].v.val.i;
	i += 3;
    }
    else if (tv[i].t == INT) {
	pin = tv[i].v.val.i;
	i++;
    }
    else {
	csp_set_error(st, ERR_SYNTAX);
	return -1;
    }
    if (!in && !out) in=1;
    if ((ix = lookup_decl(st, tv[2].v.str.ptr, tv[2].v.str.len)) == BAD_INDEX)
	ix = csp_new_decl(st, tv[2].v.str.ptr, tv[2].v.str.len, DECL_ANALOG);
    if (ix == BAD_INDEX) return -1;
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
int csp_parse_timer(csp_rt_t* st, token_t* tv, size_t n)
{
    ivalue_t init = 0;
    index_t ix, px, tx;
    int i;
    tok_t te[] = {HASH, TIMER, WORD, INT, LAST};
    
    if (!expect(tv, 0, te)) {
	csp_set_error(st, ERR_SYNTAX);	
	return -1;
    }
    if (tv[4].t == EQ) {
	if (tv[5].t != INT) { // FIXME: eval constant!
	    csp_set_error(st, ERR_SYNTAX);
	    return -1;
	}
	init = tv[5].v.val.i;
    }
    if ((px = lookup_const(st, V_INTEGER, tv[3].v.val)) == BAD_INDEX)
	px = new_signed_const(st,tv[3].v.val.i);
    tx = csp_new_decl(st, NULL, 0, DECL_VARIABLE);
    i = INDEX(tx);
    st->decl[i].vt = V_UNSIGNED;
    st->decl[i].res = MAKE_RES(32);
    st->decl[i].va.init.u = 0;
    
    if ((ix = lookup_decl(st, tv[2].v.str.ptr, tv[2].v.str.len)) == BAD_INDEX) {
	if ((ix = csp_new_decl(st, tv[2].v.str.ptr, tv[2].v.str.len, DECL_TIMER)) == BAD_INDEX)
	    return -1;
    }
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
int csp_parse_can(csp_rt_t* st, token_t* tv, size_t n)
{
    ivalue_t res = MAKE_RES(1);
    ivalue_t in=0, out=0;
    vtype_t vt = V_INTEGER;
    vendian_t endian = E_UNDEFINED;
    index_t ix;
    index_t idx;
    int i;
    int bit0, bit1;
    tok_t te[] = {HASH, CAN, WORD, LAST};
    tok_t teres[] = {COLON, INT, LAST};
    tok_t tebit[] = {INT, LB, INT, RB, LAST};
    tok_t tebitrange[] = {INT, LB, INT, DOT, DOT, INT, RB, LAST};        
    

    if (!expect(tv, 0, te)) {
	csp_set_error(st, ERR_SYNTAX);
	return -1;
    }
    i=3;
    if (expect(tv, i, teres)) {
	res = MAKE_RES(tv[i+1].v.val.i);
	i += 2;
    }
opts:
    if (i < n) {
	switch(tv[i].t) {
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

    // FrameID [bit]
    if (expect(tv, i, tebit)) {
	bit0 = bit1 = tv[i+2].v.val.i;
    }
    // FrameID [bit..bit]
    else if (expect(tv, i, tebitrange)) {
	bit0 = tv[i+2].v.val.i;
	bit1 = tv[i+5].v.val.i;
    }
    else {
	csp_set_error(st, ERR_SYNTAX);
	return -1;
    }

    // install fram id
    if ((idx = lookup_const(st, V_INTEGER, tv[i].v.val)) == BAD_INDEX)
	idx = new_signed_const(st,tv[i].v.val.i);
    
    if ((ix = lookup_decl(st, tv[2].v.str.ptr, tv[2].v.str.len)) == BAD_INDEX)
	ix = csp_new_decl(st, tv[2].v.str.ptr, tv[2].v.str.len, DECL_CAN);
    if (ix == BAD_INDEX) return -1;
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
int csp_parse_object(csp_rt_t* st, token_t* tv, size_t n)
{
    index_t mx, ix, jx;
    int i, m;
    tok_t te[] = {HASH, WORD, WORD, LAST};        

    if (!expect(tv, 0, te)) {
	csp_set_error(st, ERR_SYNTAX);
	return -1;
    }
    // lookup module
    if ((mx = lookup_decl(st, tv[1].v.str.ptr, tv[1].v.str.len)) == BAD_INDEX) {
	csp_set_error(st, ERR_MODULE_NOT_DECLARED);
	return -1;
    }
    if (st->decl[INDEX(mx)].type != DECL_MODULE) {
	csp_set_error(st, ERR_NOT_A_MODULE);
	return -1;
    }

    if (st->ps.nq >= MAX_OBJECTS-1) {
	csp_set_error(st, ERR_TOO_MANY_OBJECTS);
	return -1;
    }
    
    if ((ix = csp_new_decl(st, tv[2].v.str.ptr, tv[2].v.str.len, DECL_OBJECT)) == BAD_INDEX)
	return -1;
    if ((jx = csp_new_new(st, st->decl[INDEX(mx)].md.ent, ix)) == BAD_INDEX)
	return -1;
    i = INDEX(ix);
    st->decl[i].mq.mx = mx;
    m = st->ps.nq + 1;
    st->decl[i].mq.m = m;
    st->object[m] = ix;
    st->ps.nq++;
    return 0;
}

// <expr> '?' <cond>
// expr = tok[0]...tok[i-1]
// cond = tok[i+1]...tok[num-1]
// first parse condition
// then parse expression

// find index of t among tok or -1 if not found
static int tok_index(tok_t t, token_t* tv, size_t n)
{
    int i;
    for (i = 0; i < n; i++) {
	if (t == tv[i].t)
	    return i;
    }
    return -1;
}

//
// <rule> ==  <expr> ? <cond>
//
// generates code like
// BEGIN:
//    generate ( <cond> )
//    RULE: if !<cond> goto END
//    generate ( <expr> )
//  END:
//    next
//
int csp_parse_rule(csp_rt_t* st, token_t* tv, size_t n)
{
    size_t num;
    int i, j;
    int cnd;

    if ((i=tok_index(QUEST, tv, n)) >= 0) {
	// parse condition
	num = n - (i+1);
	if ((cnd = csp_parse_expr(st,&tv[i+1], &num)) < 0)
	    return -1;
	// parse expression after query node and patch in ex	
	num = i;
    }
    else {
	cnd = alloc_reg(st);
	if (csp_new_li(st, cnd, -1) < 0)
	    return -1;
	num = n;
    }
    if ((j = csp_new_rule(st, cnd, 0)) < 0)
	return -1;
    if (csp_parse_expr(st,&tv[0],&num) < 0)
	return -1;
    st->instr[j].r.nxt = st->ps.nn;
    if (csp_new_next(st) < 0)
	return -1;
    return 0;
}

index_t lookup_can_range(csp_rt_t* st, index_t idx, ivalue_t p0, ivalue_t p1)
{
    index_t i;
    for (i = 0; i < st->ps.nd; i++) {
	if (IS_CAN(st, i) && (idx == st->decl[i].ca.id)) {
	    if ((st->decl[i].ca.bit == p0) &&
		(st->decl[i].ca.len == MAKE_CAN_LEN((p1-p0)+1)))
		return MAKE_INDEX(0,i);
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
    // if (st->ni >= MAX_INPUTS) return BAD_INDEX;
    //   st->input[st->ni++] = ix;
    return ix;
}

// make legacy CAN rule for one set or clr expression
// <or> = <k> ? (<idx>[p0] == <c>)
int make_can_rule(csp_rt_t* st, index_t ox, int k, index_t idx,
		  int byte, int bit, int16_t c)
{
    int cnd, cr, zr, kr, j;
    index_t zx;
    int p0 = byte*8 + bit;

    // load constant C into cr
    cr = alloc_reg(st);
    if (csp_new_li(st, cr, c) < 0)
	return -1;    
    // load constant k into kr
    kr = alloc_reg(st);
    if (csp_new_li(st, kr, k) < 0)
	return -1;

    // first build condition (can bit test)
    if ((zx = lookup_can_range(st, idx, p0, p0)) == BAD_INDEX) {
	if ((zx = make_can_range(st, NULL, 0, idx, p0, p0)) == BAD_INDEX)
	    return -1;
    }
    // load zx into register
    zr = alloc_reg(st);
    if (csp_new_ld(st,zr,zx) < 0)
	return -1;

    cnd = alloc_reg(st);
    if (new_expr2(st, OP_EQEQ, cnd, zr, cr) < 0)
	return -1;

    if ((j = csp_new_rule(st, cnd, 0)) < 0)
	return -1;
    if (csp_new_st(st, kr, ox) < 0)
	return -1;
    st->instr[j].r.nxt = st->ps.nn;    
    if (csp_new_next(st) < 0)
	return -1;    
    return 0;
}

// FrameID BytePos Mask OnBits OffBits
int csp_parse_legacy(csp_rt_t* st, token_t* tv, size_t n)
{
    index_t out;
    index_t idx;        
    ivalue_t pos;    
    ivalue_t mask;
    ivalue_t on_bits;
    ivalue_t off_bits;    
    int i;
    tok_t te[] = {INT, INT, INT, INT, INT, LAST};

    if (!expect(tv, 0, te))
	return -1;
    pos = tv[1].v.val.i;
    mask = tv[2].v.val.i;
    on_bits = tv[3].v.val.i;
    off_bits = tv[4].v.val.i;
    
    if ((out = lookup_decl(st, "OUT", 3)) == BAD_INDEX) {
	// fixme: pin number etc for standard OUT
	if ((out = csp_new_decl(st,"OUT",3,DECL_DIGITAL)) == BAD_INDEX)
	    return -1;
    }

    if ((idx = lookup_const(st, V_INTEGER, tv[0].v.val)) == BAD_INDEX)
	idx = new_signed_const(st,tv[0].v.val.i);    
    

    // OUT = 1
    for (i = 7; i >= 0; i--) {
	uint8_t bit = (1 << i);
	if ((mask & bit) && (on_bits & bit)) {
	    if (make_can_rule(st, out, 1, idx, pos, i, 1) < 0)
		return -1;
	}
    }

    // OUT = 0
    for (i = 7; i >= 0; i--) {
	uint8_t bit = (1 << i);	
	if ((mask & bit) && !(off_bits & bit)) {
	    if (make_can_rule(st, out, 0, idx, pos, i, 0) < 0)
		return -1;
	}
    }
    return 0;
}

// '>' command
int csp_parse_immediate(csp_rt_t* st, token_t* tv, size_t n)
{
    return 0;
}
    
int csp_parse(csp_rt_t* st, char* str)
{
    token_t tv[MAX_LINE_TOKENS];
    size_t num = MAX_LINE_TOKENS;
    reg_allocator_t alloc;
    int n;
    tok_t te[] = {INT, INT, INT, INT, INT, LAST};
    
    st->ap = &alloc;
        
    while((n = csp_scan_line(str, tv, &num)) > 0) {
	int r = -1;
	str += n;
	alloc_init(st->ap);
    
	if (tv[0].t == NEWLINE)
	    r = 0;
	else if (expect(tv, 0, te)) {
	    r = csp_parse_legacy(st, tv, num);
	}
	else if (tv[0].t == HASH) {
	    switch(tv[1].t) {
	    case MODULE:  // '#' 'module' WORD
		r = csp_parse_module(st, tv, num);
		break;
	    case END:
		r = csp_parse_end(st, tv, num);
		break;
	    case VARIABLE:
		r = csp_parse_variable(st, tv, num);
		break;
	    case CONSTANT:
		r = csp_parse_constant(st, tv, num);
		break;
	    case DIGITAL:
		r = csp_parse_digital(st, tv, num);
		break;
	    case ANALOG:
		r = csp_parse_analog(st, tv, num);
		break;
	    case TIMER:
		r = csp_parse_timer(st, tv, num);
		break;
	    case CAN:
		r = csp_parse_can(st, tv, num);
		break;
	    case WORD: // module instantiation?
		r = csp_parse_object(st, tv, num);
		break;
	    default:
		return -1;
	    }
	}
	else if (tv[0].t == GT) {
	    r = csp_parse_immediate(st, tv, num);
	}
	else {
	    r = csp_parse_rule(st, tv, num);
	}
	if (r < 0)
	    return -1;
	st->ps.line++;
	num = MAX_LINE_TOKENS;
    }
    return 0;
}

int csp_rt_init(csp_rt_t* st, int transaction, int reactive)
{
    memset(st, 0x00, sizeof(csp_rt_t));

    // st->xin = st->xout = st->xv0;
    st->din = st->dout = st->dv0;    

    st->reactive = reactive;
    if ((st->transaction = transaction) != 0) {
#if defined(SUPPORT_TRANSACTION) && (SUPPORT_TRANSACTION==1)
	// st->xout = st->xv1;
	st->dout = st->dv1;    	
#endif
    }
    
    st->ps.nn = 0;
    st->ps.nd = 0;
    st->ps.nq = 0;
    st->ps.strp = 1;
    st->ps.err  = ERR_OK;
    st->ps.line = 0;

    st->nt = 0;
    st->ni = 0;
    st->no = 0;
    st->nm = 0;
    st->cur = 0;     // current module = global

    st->str[0] = 0;  // reserved 0 and nil
    st->ufuncs = NULL;
    st->num_ufuncs = 0;
    st->uconst = NULL;
    new_signed_const(st, 0);
    new_signed_const(st, 1);
    return 0;
}

// Set user function table (called before parsing)
void csp_set_ufuncs(csp_rt_t* st, const csp_func_t* funcs, uint8_t count)
{
    st->ufuncs = funcs;
    st->num_ufuncs = count;
}

void csp_set_uconst(csp_rt_t* st, csp_const_fn uconst)
{
    st->uconst = uconst;
}

// copy constant and init values
// setup input, output and timer lists
//
int csp_rt_start(csp_rt_t* st)
{
    int i;
    int offs;
    
#if defined(SUPPORT_REACTIVE) && (SUPPORT_REACTIVE==1)    
    st->tl = st->hd = 0;
#endif
    st->nt = 0;
    st->ni = 0;
    st->no = 0;
    st->nm = 0;
    
    for (i = 0; i < st->ps.nd; i++) {
	index_t ix = MAKE_INDEX(0,i);
	switch(st->decl[i].type) {
	case DECL_MODULE:
	    if (st->nm < MAX_MODULES)
		st->module[st->nm++] = ix;
	    break;
	case DECL_OBJECT:
	    break;	    
	case DECL_CONSTANT:
	    st->din[i] = st->dout[i] = st->decl[i].cn.init;
	    break;
	case DECL_VARIABLE:
	    st->din[i] = st->decl[i].va.init;
	    csp_set_value(st, ix, st->din[i]);
	    break;
	case DECL_TIMER:
	    if (st->decl[i].tm.init == 1) {
		int tj = st_index(st, st->decl[i].tm.tx);
		st->decl[i].tm.running = 1;
		st->din[tj].u = st->dout[tj].u = csp_time_ms();
		csp_set_ivalue(st, ix, 1);
	    }
	    st->timer[st->nt++] = ix;
	    break;
	case DECL_DIGITAL:
	case DECL_ANALOG:
	case DECL_CAN:
	    if (st->decl[i].in) {
		if (st->ni < MAX_INPUTS) // warning?
		    st->input[st->ni++] = ix;
	    }
	    if (st->decl[i].out) {
		if (st->no < MAX_OUTPUTS) // warning
		    st->output[st->no++] = ix;
	    }	    
	    break;
	default:
	    break;
	}
    }
    // allocate object 1..nq storage
    offs = st->ps.nd;
    for (i = 0; i < st->ps.nq; i++) {
	int m = i+1;
	index_t ix = st->object[m];
	index_t mx = st->decl[INDEX(ix)].mq.mx;  // module def
	ivalue_t dn = st->decl[INDEX(mx)].md.n;  // number of decl elements
	st->offs[m] = offs;
	offs += dn;
	if (offs > MAX_DECLS) // set error
	    return -1;
    }
    return 0;
}

int csp_set_transaction(csp_rt_t* st, int onoff)
{
#if defined(SUPPORT_TRANSACTION) && (SUPPORT_TRANSACTION==1)    
    if ((st->transaction = onoff) != 0) {
	st->dout = (st->din == st->dv0) ? st->dv1 : st->dv0;
    }
    return 0;
#endif
    return -1;
}

int csp_set_reactive(csp_rt_t* st, int onoff)
{
#if defined(SUPPORT_REACTIVE) && (SUPPORT_REACTIVE==1)
    st->reactive = onoff;
    return 0;
#endif
    return -1;    
}
