// CandySpeak runtime
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "csp.h"
#include "csp_parse.h"
#ifdef DEBUG
#include "csp_dump.h"
#include <stdio.h>
extern int debug;
#endif

#define CAT_HELPER2(x,y) x ## y
#define CAT2(x,y) CAT_HELPER2(x,y)

// convert integer to -1 if y != 0  0 otherwise
#define BOOL(y) (-((y)!=0))

// CTYPE
#define ISDIGIT(c) (((c) >= '0') && ((c) <= '9'))
#define ISUPPER(c) (((c) >= 'A') && ((c) <= 'Z'))
#define ISLOWER(c) (((c) >= 'a') && ((c) <= 'z'))
#define ISXUPPER(c) (((c) >= 'A') && ((c) <= 'F'))
#define ISXLOWER(c) (((c) >= 'a') && ((c) <= 'f'))
#define ISXDIGIT(c) (ISDIGIT((c)) || ISXUPPER((c)) || ISXLOWER((c)))
#define ISALPHA(c) (ISUPPER((c)) || ISLOWER((c)))

// assoc
#define LEFT -1
#define RIGHT 1
#define NO    0

// string length for constant strings "foo" => 3
#define CSTRLEN(str) (sizeof((str))-1)

#define TOK_ENT(o,c,n) \
    [(o)] = { .tok=(o),.code=(c),.name=(n),.namelen=CSTRLEN((n)),.arity=-1,.prec=-1,.assoc=NO }

#define INSTR_ENT(o,c,n,a,p,s) \
    [(o)] = { .tok=(o),.code=(c),.name=(n),.namelen=CSTRLEN((n)),.arity=(a),.prec=(p),.assoc=(s) }

#define DECL_ENT(o,c,n) \
    [(o)] = { .tok=(o),.code=(c),.name=(n),.namelen=CSTRLEN((n)),.arity=-1,.prec=-1,.assoc=NO }

static rochar s_null[] RODATA = "";
static rochar s_module[] RODATA = "module";
static rochar s_end[] RODATA = "end";
#if defined(SUPPORT_STATES) && (SUPPORT_STATES==1)
static rochar s_states[] RODATA = "states";
#endif
static rochar s_constant[] RODATA = "constant";
static rochar s_variable[] RODATA = "variable";
static rochar s_digital[] RODATA = "digital";
static rochar s_analog[] RODATA = "analog";
static rochar s_timer[] RODATA = "timer";
static rochar s_can[] RODATA = "can";
static rochar s_object[] RODATA = "object";
static rochar s_EXCLAMATION[] RODATA = "!";
static rochar s_TILDE[] RODATA = "~";
static rochar s_MINUS[] RODATA = "-";
static rochar s_PLUS[] RODATA = "+";
static rochar s_ASTERISK[] RODATA = "*";
static rochar s_SLASH[] RODATA = "/";
static rochar s_PERCENT[] RODATA = "%";
static rochar s_LTLT[] RODATA = "<<";
static rochar s_GTGT[] RODATA = ">>";
static rochar s_LT[] RODATA = "<";
static rochar s_LTEQ[] RODATA = "<=";
static rochar s_RIMP[] RODATA = "<-";
static rochar s_GT[] RODATA = ">";
static rochar s_GTEQ[] RODATA = ">=";
static rochar s_EQEQ[] RODATA = "==";
static rochar s_NEQ[] RODATA = "!=";
static rochar s_AMP[] RODATA = "&";
static rochar s_CIRC[] RODATA = "^";
static rochar s_BAR[] RODATA = "|";
static rochar s_AMPAMP[] RODATA = "&&";
static rochar s_BARBAR[] RODATA = "||";
static rochar s_EQ[] RODATA = "=";
static rochar s_COMMA[] RODATA = ",";
static rochar s_QUEST[] RODATA = "?";
static rochar s_next[] RODATA = "next";
static rochar s_enter[] RODATA = "enter";
static rochar s_leave[] RODATA = "leave";
static rochar s_new[] RODATA = "new";
static rochar s_call[] RODATA = "call";
static rochar s_ld[] RODATA = "ld";
static rochar s_st[] RODATA = "st";
static rochar s_stp[] RODATA = "stp";
static rochar s_stimp[] RODATA = "stimp";
static rochar s_li[] RODATA = "li";
static rochar s_chg[] RODATA = "chg";
static rochar s_arg[] RODATA = "arg";
static rochar s_cvtif[] RODATA = "cvtif";
static rochar s_cvtfi[] RODATA = "cvtfi";
static rochar s_pullup[] RODATA = "pullup";
static rochar s_pulldown[] RODATA = "pulldown";
static rochar s_resolution[] RODATA = "resolution";
static rochar s_undefined[] RODATA = "undefined";
static rochar s_none[] RODATA = "none";
static rochar s_in[] RODATA = "in";
static rochar s_out[] RODATA = "out";
static rochar s_inout[] RODATA = "inout";
static rochar s_pwm[] RODATA = "pwm";
static rochar s_void[] RODATA = "void";
static rochar s_float[] RODATA = "float";
static rochar s_integer[] RODATA = "integer";
static rochar s_unsigned[] RODATA = "unsigned";
static rochar s_index[] RODATA = "index";
static rochar s_number[] RODATA = "number";
static rochar s_string[] RODATA = "string";
static rochar s_any[] RODATA = "any";
static rochar s_little[] RODATA = "little";
static rochar s_big[] RODATA = "big";
static rochar s_LP[] RODATA = "(";
static rochar s_RP[] RODATA = ")";
static rochar s_HASH[] RODATA = "#";
static rochar s_DOT[] RODATA = ".";
static rochar s_COLON[] RODATA = ":";
static rochar s_LB[] RODATA = "[";
static rochar s_RB[] RODATA = "]";
static rochar s_MOV[] RODATA = "mov";

const op_entry_t op_table[] RODATA = {
    TOK_ENT(NONE,OP_NOP,s_null),
    // leaf
    DECL_ENT(MODULE,DECL_MODULE,s_module),
    DECL_ENT(END,DECL_END, s_end),
    DECL_ENT(CONSTANT,DECL_CONSTANT,s_constant),
    DECL_ENT(VARIABLE,DECL_VARIABLE,s_variable),
    DECL_ENT(DIGITAL,DECL_DIGITAL,s_digital),
    DECL_ENT(ANALOG,DECL_ANALOG,s_analog),
    DECL_ENT(TIMER,DECL_TIMER,s_timer),
    DECL_ENT(CAN,DECL_CAN,s_can),
    // node - unary
    INSTR_ENT(EXCLAMATION,OP_NOT,s_EXCLAMATION,1,105,RIGHT),
    INSTR_ENT(TILDE,OP_BNOT,s_TILDE,1,105,RIGHT),
    INSTR_ENT(MINUS1,OP_NEG,s_MINUS,1,105,RIGHT),
    INSTR_ENT(PLUS1,OP_MOV,s_MOV,1,105,RIGHT),
    // node - binary
    INSTR_ENT(PLUS,OP_ADD,s_PLUS,2,90,LEFT),
    INSTR_ENT(MINUS,OP_SUB,s_MINUS,2,90,LEFT),
    INSTR_ENT(ASTERISK,OP_MUL,s_ASTERISK,2,100,LEFT),
    INSTR_ENT(SLASH,OP_DIV,s_SLASH,2,100,LEFT),
    INSTR_ENT(PERCENT,OP_REM,s_PERCENT,2,100,LEFT),
    INSTR_ENT(LTLT,OP_SLA,s_LTLT,2,80,LEFT),
    INSTR_ENT(GTGT,OP_SRA,s_GTGT,2,80,LEFT),
    INSTR_ENT(LT,OP_LT,s_LT,2,70,LEFT),
    INSTR_ENT(LTEQ,OP_LTE,s_LTEQ,2,70,LEFT),
    INSTR_ENT(GT,OP_GT,s_GT,2,70,LEFT),
    INSTR_ENT(GTEQ,OP_GTE,s_GTEQ,2,70,LEFT),
    INSTR_ENT(EQEQ,OP_EQEQ,s_EQEQ,2,60,LEFT),
    INSTR_ENT(NEQ,OP_NEQ,s_NEQ,2,60,LEFT),
    INSTR_ENT(AMP,OP_BAND,s_AMP,2,50,LEFT),
    INSTR_ENT(CIRC,OP_BXOR,s_CIRC,2,40,LEFT),
    INSTR_ENT(BAR,OP_BOR,s_BAR,2,30,LEFT),
    INSTR_ENT(AMPAMP,OP_AND,s_AMPAMP,2,20,LEFT),
    INSTR_ENT(BARBAR,OP_OR,s_BARBAR,2,10,LEFT),
    INSTR_ENT(EQ,OP_EQ,s_EQ,2,5,RIGHT),
    INSTR_ENT(RIMP,OP_RIMP,s_RIMP,2,4,RIGHT),
    INSTR_ENT(COMMA,OP_COMMA,s_COMMA,2,2,RIGHT),
    INSTR_ENT(QUEST,OP_RULE,s_QUEST,-1,-1,NO),

    INSTR_ENT(NEXT,OP_NEXT,s_next,-1,-1,NO),

    // OP_ENTER: y=<num-instr>, z=DECL:module-index
    INSTR_ENT(ENTER,OP_ENTER,s_enter,-1,-1,NO),
    // OP_ENTER: y=<num-instr>, z=DECL:module-index
    INSTR_ENT(LEAVE,OP_LEAVE,s_leave,-1,-1,NO),
    // OP_NEW: y=INSTR:enter-index, z=DECL:mod-index
    INSTR_ENT(NEW,OP_NEW,s_new,-1,-1,NO),
    // functions are now looked up via csp_lookup_func() + csp_builtin_funcs[]
    INSTR_ENT(CALL,OP_CALL,s_call,-1,-1,NO),
    INSTR_ENT(LD,OP_LD,s_ld,-1,-1,NO),
    INSTR_ENT(ST,OP_ST,s_st,-1,-1,NO),
    INSTR_ENT(STP,OP_STP,s_stp,-1,-1,NO),    
    INSTR_ENT(STIMP,OP_STIMP,s_stimp,-1,-1,NO),
    INSTR_ENT(CHG,OP_CHG,s_chg,-1,-1,NO),
    INSTR_ENT(LI,OP_LI,s_li,-1,-1,NO),
    INSTR_ENT(ARG,OP_ARG,s_arg,-1,-1,NO),
    INSTR_ENT(CVTIF,OP_CVTIF,s_cvtif,-1,-1,NO),
    INSTR_ENT(CVTIF,OP_CVTFI,s_cvtfi,-1,-1,NO),

    // keywords
#if defined(SUPPORT_STATES) && (SUPPORT_STATES==1)    
    TOK_ENT(STATES,OP_NOP,s_states),
#endif
    TOK_ENT(PULLUP,OP_NOP,s_pullup),
    TOK_ENT(PULLDOWN,OP_NOP,s_pulldown),
    TOK_ENT(RESOLUTION,OP_NOP,s_resolution),
    TOK_ENT(IN,OP_NOP,s_in),
    TOK_ENT(OUT,OP_NOP,s_out),
    TOK_ENT(INOUT,OP_NOP,s_inout),
    TOK_ENT(PWM,OP_NOP,s_pwm),
    TOK_ENT(FLOAT,OP_NOP,s_float),
    TOK_ENT(INTEGER,OP_NOP,s_integer),
    TOK_ENT(UNSIGNED,OP_NOP,s_unsigned),
    TOK_ENT(STRING,OP_NOP,s_string),
    TOK_ENT(LITTLE,OP_NOP,s_little),
    TOK_ENT(BIG,OP_NOP,s_big),

    // tokens
    TOK_ENT(LP,OP_NOP,s_LP),
    TOK_ENT(RP,OP_NOP,s_RP),
    TOK_ENT(HASH,OP_NOP,s_HASH),
    TOK_ENT(DOT,OP_NOP,s_DOT),
    TOK_ENT(COLON,OP_NOP,s_COLON),
    TOK_ENT(LB,OP_NOP,s_LB),
    TOK_ENT(RB,OP_NOP,s_RB),
    TOK_ENT(INT,OP_NOP,s_null),
    TOK_ENT(FLT,OP_NOP,s_null),
    TOK_ENT(WORD,OP_NOP,s_null),
    TOK_ENT(NEWLINE,OP_NOP,s_null),
    // eot
    TOK_ENT(LAST,OP_NOP,s_null)
};


// Function calls are stored in ostack as (LAST + 1 + func_index)
// func_index encodes: (index << 1) | is_user
// Note: must use LAST (not LAST_NODE) to avoid overlap with LP, RP, etc.
// Fixme: (fname-token-index:8,ostack-depth:8,last:8)
// make ostack uint32_t
#define FUNC_MARKER_BASE (LAST + 1)
#define IS_FUNC_MARKER(op) ((op) >= FUNC_MARKER_BASE)
#define MAKE_FUNC_MARKER(tix, pp0) ((FUNC_MARKER_BASE) +  \
				    ((tix)<<16) + ((pp0)<< 8))
#define FUNC_MARKER_TIX(op)  (((op) >> 16) & 0xff)
#define FUNC_MARKER_EP(op)   (((op) >> 8) & 0x0ff)


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
MAKE_FF(FMOV);

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
MAKE_II(MOV);
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

static rochar s_ADD[] RODATA = "ADD";
static rochar s_SUB[] RODATA = "SUB";
static rochar s_MUL[] RODATA = "MUL";
static rochar s_DIV[] RODATA = "DIV";
static rochar s_REM[] RODATA = "REM";
static rochar s_SLA[] RODATA = "SLA";
static rochar s_SRA[] RODATA = "SRA";
static rochar s_BAND[] RODATA = "BAND";
static rochar s_BOR[] RODATA = "BOR";
static rochar s_BXOR[] RODATA = "BXOR";
static rochar s_AND[] RODATA = "AND";
static rochar s_OR[] RODATA = "OR";
static rochar s_ASSIGN[] RODATA = "ASSIGN";
static rochar ss_LT[] RODATA = "LT";
static rochar ss_LTE[] RODATA = "LTE";
static rochar ss_GT[] RODATA = "GT";
static rochar ss_GTE[] RODATA = "GTE";
static rochar ss_EQ[] RODATA = "EQ";
static rochar ss_NEQ[] RODATA = "NEQ";
static rochar s_BNOT[] RODATA = "BNOT";
static rochar s_NEG[] RODATA = "NEG";
static rochar s_NOT[] RODATA = "NOT";
static rochar ss_MOV[] RODATA = "MOV";
static rochar s_CVTIF[] RODATA = "CVTIF";
static rochar s_CVTFI[] RODATA = "CVTFI";

static rochar s_FADD[] RODATA = "FADD";
static rochar s_FSUB[] RODATA = "FSUB";
static rochar s_FMUL[] RODATA = "FMUL";
static rochar s_FDIV[] RODATA = "FDIV";
static rochar s_FNEG[] RODATA = "FNEG";
static rochar s_FMOV[] RODATA = "FMOV";

static rochar s_FLT[] RODATA = "FLT";
static rochar s_FLTE[] RODATA = "FLTE";
static rochar s_FGT[] RODATA = "FGT";
static rochar s_FGTE[] RODATA = "FGTE";
static rochar s_FEQ[] RODATA = "FEQ";
static rochar s_FNEQ[] RODATA = "FNEQ";

static rochar ss_COMMA[] RODATA = "COMMA";

static rochar s_ENTER[] RODATA = "ENTER";
static rochar s_LEAVE[] RODATA = "LEAVE";
static rochar s_NEW[] RODATA = "NEW";
static rochar s_LI[] RODATA = "LI";
static rochar s_LIU[] RODATA = "LIU";
static rochar s_LIH[] RODATA = "LIH";
static rochar s_ARG[] RODATA = "ARG";
static rochar s_ST[] RODATA = "ST";
static rochar s_STP[] RODATA = "STP";
static rochar s_STIMP[] RODATA = "STIMP";
static rochar s_CHG[] RODATA = "CHG";
static rochar s_LD[] RODATA = "LD";
static rochar s_CALL[] RODATA = "CALL";
static rochar s_RULE[] RODATA = "RULE";
static rochar s_NEXT[] RODATA = "NEXT";
static rochar s_NOP[] RODATA = "NOP";

// opcode => opcode type info
static const op_info_t info_tab[] RODATA = {
    [OP_ADD] = {s_ADD,2,V_INTEGER,{V_INTEGER,V_INTEGER}},
    [OP_SUB] = {s_SUB,2,V_INTEGER,{V_INTEGER,V_INTEGER}},
    [OP_MUL] = {s_MUL,2,V_INTEGER,{V_INTEGER,V_INTEGER}},
    [OP_DIV] = {s_DIV,2,V_INTEGER,{V_INTEGER,V_INTEGER}},
    [OP_REM] = {s_REM,2,V_INTEGER,{V_INTEGER,V_INTEGER}},
    [OP_SLA] = {s_SLA,2,V_INTEGER,{V_INTEGER,V_INTEGER}},
    [OP_SRA] = {s_SRA,2,V_INTEGER,{V_INTEGER,V_INTEGER}},
    [OP_BAND] = {s_BAND,2,V_INTEGER,{V_INTEGER,V_INTEGER}},
    [OP_BOR] = {s_BOR,2,V_INTEGER,{V_INTEGER,V_INTEGER}},
    [OP_BXOR] = {s_BXOR,2,V_INTEGER,{V_INTEGER,V_INTEGER}},
    [OP_AND] = {s_AND,2,V_INTEGER,{V_INTEGER,V_INTEGER}},
    [OP_OR] = {s_OR,2,V_INTEGER,{V_INTEGER,V_INTEGER}},
    [OP_EQ] = {s_ASSIGN,2,V_INTEGER,{V_INDEX,V_INTEGER}},
    [OP_LT] = {ss_LT,2,V_INTEGER,{V_INTEGER,V_INTEGER}},
    [OP_LTE] = {ss_LTE,2,V_INTEGER,{V_INTEGER,V_INTEGER}},
    [OP_GT] = {ss_GT,2,V_INTEGER,{V_INTEGER,V_INTEGER}},
    [OP_GTE] = {ss_GTE,2,V_INTEGER,{V_INTEGER,V_INTEGER}},
    [OP_EQEQ] = {ss_EQ,2,V_INTEGER,{V_INTEGER,V_INTEGER}},
    [OP_NEQ] = {ss_NEQ,2,V_INTEGER,{V_INTEGER,V_INTEGER}},
    // unary versions (treated as binary with z ignored)
    [OP_BNOT] = {s_BNOT,1,V_INTEGER,{V_INTEGER}},
    [OP_NEG] = {s_NEG,1,V_INTEGER,{V_INTEGER}},
    [OP_MOV] = {ss_MOV,1,V_INTEGER,{V_INTEGER}},
    [OP_NOT] = {s_NOT,1,V_INTEGER,{V_INTEGER}},
    [OP_CVTIF] = {s_CVTIF,1,V_FLOAT,{V_INTEGER}},   // int→float
    [OP_CVTFI] = {s_CVTFI,1,V_INTEGER,{V_FLOAT}},  // float→int

    [OP_FNEG] = {s_FNEG,1,V_FLOAT,{V_FLOAT}},
    [OP_FMOV] = {s_FMOV,1,V_FLOAT,{V_FLOAT}},
    [OP_FADD] = {s_FADD,2,V_FLOAT,{V_FLOAT,V_FLOAT}},
    [OP_FSUB] = {s_FSUB,2,V_FLOAT,{V_FLOAT,V_FLOAT}},
    [OP_FMUL] = {s_FMUL,2,V_FLOAT,{V_FLOAT,V_FLOAT}},
    [OP_FDIV] = {s_FDIV,2,V_FLOAT,{V_FLOAT,V_FLOAT}},

    [OP_FLT] = {s_FLT,2,V_INTEGER,{V_FLOAT,V_FLOAT}},
    [OP_FLTE] = {s_FLTE,2,V_INTEGER,{V_FLOAT,V_FLOAT}},
    [OP_FGT] = {s_FGT,2,V_INTEGER,{V_FLOAT,V_FLOAT}},
    [OP_FGTE] = {s_FGTE,2,V_INTEGER,{V_FLOAT,V_FLOAT}},
    [OP_FEQEQ] = {s_FEQ,2,V_INTEGER,{V_FLOAT,V_FLOAT}},
    [OP_FNEQ] = {s_FNEQ,2,V_INTEGER,{V_FLOAT,V_FLOAT}},

    // comman may not be needed?
    [OP_COMMA] = {ss_COMMA,2,V_INTEGER,{V_INTEGER,V_INTEGER}},

    // other operations for name
    [OP_ENTER] = {s_ENTER,0,V_VOID,{}},
    [OP_LEAVE] = {s_LEAVE,0,V_VOID,{}},
    [OP_NEW]   = {s_NEW,0,V_VOID,{}},
    [OP_LI]    = {s_LI,0,V_VOID,{}},
    [OP_LIU]   = {s_LIU,0,V_VOID,{}},
    [OP_LIH]   = {s_LIH,0,V_VOID,{}},
    [OP_ARG]   = {s_ARG,0,V_VOID,{}},
    [OP_ST]    = {s_ST,0,V_VOID,{}},
    [OP_STP]   = {s_STP,0,V_VOID,{}},    
    [OP_STIMP] = {s_STIMP,0,V_VOID,{}},
    [OP_CHG]   = {s_CHG,0,V_VOID,{}},
    [OP_LD]    = {s_LD,0,V_VOID,{}},
    [OP_CALL]  = {s_CALL,0,V_VOID,{}},
    [OP_RULE]  = {s_RULE,0,V_VOID,{}},
    [OP_NEXT]  = {s_NEXT,0,V_VOID,{}},
    [OP_NOP] = {s_NOP,0,V_VOID,{}},

};

static const char tag_tab[] RODATA = {
    [DECL_OBJECT] = 'q',
    [DECL_MODULE] = 'm',
    [DECL_CONSTANT] = 'c',
    [DECL_VARIABLE] = 'v',
    [DECL_DIGITAL] = 'd',
    [DECL_ANALOG] = 'a',
    [DECL_TIMER] = 't',
    [DECL_CAN] = 'k',
};

const char csp_tag(csp_rt_t* st, index_t n)
{
    return tag_tab[st->decl[INDEX(n)].type];
}

static rochar* const pindir_tab[] RODATA = {
    [DIR_NONE] = s_none,
    [DIR_IN]   = s_in,
    [DIR_OUT]  = s_out,
    [DIR_INOUT]  = s_inout
};

rochar* csp_fmt_pindir(uint8_t dir)
{
    return RD_PTR(&pindir_tab[dir&0x3]);
}

rochar* csp_fmt_pull(csp_rt_t* st, int ix)
{
    if (st->decl[ix].di.pullup)
	return s_pullup;
    else if (st->decl[ix].di.pulldown)
	return s_pulldown;
    else
	return s_undefined;  // floating
}

rochar* csp_fmt_pwm(csp_rt_t* st, int ix)
{
    if (st->decl[ix].an.pwm)
	return s_pwm;
    else
	return s_undefined;
}

static rochar* const vtype_tab[] RODATA = {
    [V_VOID] = s_void,
    [V_INTEGER] = s_integer,
    [V_UNSIGNED] = s_unsigned,
    [V_FLOAT] = s_float,
    [V_STRING] = s_string,
    [V_INDEX] = s_index,
    [V_NUMBER] = s_number,
    [V_ANY] = s_any,
    [V_DIGITAL] = s_digital,
    [V_ANALOG] = s_analog,
    [V_TIMER] = s_timer,
    [V_CAN] = s_can,
};

const rochar* csp_fmt_vtype(vtype_t vt)
{
    return (rochar*) RD_PTR(&vtype_tab[vt & 0xf]);
}

static const char* const endian_tab[] RODATA = {
    [E_UNDEFINED] = s_undefined,
    [E_LITTLE] = s_little,
    [E_BIG] = s_big,
    [0x3] = s_undefined
};

const char* csp_fmt_endian(vendian_t et)
{
    return endian_tab[et&0x3];
}

#define RETURN_TSTR(s_str) { \
	tstr_t str = {.ptr=(char*)(s_str),.len=sizeof((s_str))-1 };	\
	return str;							\
    }
    
const tstr_t decl_type_name(decl_t type)
{
    switch(type) {
    case DECL_VARIABLE: RETURN_TSTR(s_variable);
    case DECL_CONSTANT: RETURN_TSTR(s_constant);
    case DECL_MODULE:   RETURN_TSTR(s_module);
    case DECL_END:      RETURN_TSTR(s_end);
    case DECL_OBJECT:   RETURN_TSTR(s_object);
    case DECL_TIMER:    RETURN_TSTR(s_timer);
    case DECL_DIGITAL:  RETURN_TSTR(s_digital);
    case DECL_ANALOG:   RETURN_TSTR(s_analog);
    case DECL_CAN:      RETURN_TSTR(s_can);
    default: RETURN_TSTR(s_undefined);
    }
}

const char* csp_format_error(csp_err_t err)
{
    switch(err) {
    case ERR_OK:
	return "ok";
    case ERR_SYNTAX:
	return "syntax error";
    case ERR_STRING_SPACE_EXHUSTED:
	return "string space exhuasted";
    case ERR_TOO_MANY_DECLARATIONS:
	return "too many declarations";
    case ERR_TOO_MANY_INSTRUCTIONS:
	return "too many instructions";
    case ERR_TOO_MANY_OBJECTS:
	return "too many objects";	
    case ERR_MODULE_NOT_DECLARED:
	return "module %s not declared";
#if defined(SUPPORT_STATES) && (SUPPORT_STATES==1)
    case ERR_TOO_MANY_STATES:
	return "too many states";
    case ERR_STATE_NOT_DECLARED:
	return "state %s not declared";
#endif
    case ERR_NOT_A_MODULE:
	return "word %s not a module";
    case ERR_END_MISMATCH:
	return "end mismatch";	
    case ERR_OBJECT_NOT_DEFINED:
	return "object %s is not defined";
    case ERR_VARIABLE_NOT_DECLARED:
	return "variable %s is not declared";
    case ERR_FIELD_NOT_FOUND:
	return "field %s not found";
    case ERR_FUNCTION_DOES_NOT_EXIST:
	return "function %s/%d does not exist";
    case ERR_ALREADY_DEFINED:
	return "%s %s is already defined";  // <type> <name> is alread defined
    case ERR_INTERNAL_ERROR:
	return "internal error";
    case ERR_FUNCTION_ARGUMENT_TYPE_MISMATCH:
	return "function %s/%d argument %d type mismatch";
    default:
	return "unknown error";
    }
}

static NOINLINE value_t eval0(opcode_t op)
{
    switch(op) {
    default: {
	value_t x = {.i = 0 };
	// emit error signal somehow ?
	return x;
    }
    }
}

static NOINLINE value_t eval1(opcode_t op, value_t y)
{
    switch(op) {
    case OP_BNOT: return f_BNOT(y);
    case OP_NEG:  return f_NEG(y);
    case OP_MOV:  return f_MOV(y);
    case OP_NOT:  return f_NOT(y);
    case OP_CVTIF: return f_CVTIF(y);
    case OP_CVTFI: return f_CVTFI(y);
    case OP_FNEG:  return f_FNEG(y);
    case OP_FMOV:  return f_FMOV(y);
    default: {
	value_t x = {.i = 0 };
	// emit error signal somehow ?
	return x;
    }
    }
}

static NOINLINE value_t eval2(opcode_t op, value_t y, value_t z)
{
    switch(op) {
    case OP_ADD: return f_ADD(y, z);
    case OP_SUB: return f_SUB(y, z);
    case OP_MUL: return f_MUL(y, z);
    case OP_DIV: return f_DIV(y, z);
    case OP_REM: return f_REM(y, z);
    case OP_SLA: return f_SLA(y, z);
    case OP_SRA: return f_SRA(y, z);
    case OP_BAND: return f_BAND(y, z);
    case OP_BOR: return f_BOR(y, z);
    case OP_BXOR: return f_BXOR(y, z);
    case OP_AND: return f_AND(y, z);
    case OP_OR: return f_OR(y, z);
    case OP_LT: return f_LT(y, z);
    case OP_LTE: return f_LTE(y, z);
    case OP_GT: return f_GT(y, z);
    case OP_GTE: return f_GTE(y, z);
    case OP_EQEQ: return f_EQEQ(y, z);
    case OP_NEQ: return f_NEQ(y, z);

    case OP_FADD: return f_FADD(y, z);
    case OP_FSUB: return f_FSUB(y, z);
    case OP_FMUL: return f_FMUL(y, z);
    case OP_FDIV: return f_FDIV(y, z);

    case OP_FLT: return f_FLT(y, z);
    case OP_FLTE: return f_FLTE(y, z);
    case OP_FGT: return f_FGT(y, z);
    case OP_FGTE: return f_FGTE(y, z);
    case OP_FEQEQ: return f_FEQEQ(y, z);
    case OP_FNEQ: return f_FNEQ(y, z);

    case OP_COMMA: return f_COMMA(y, z);
    default: {
	value_t x = {.i = 0 };
	// emit error signal somehow ?
	return x;
    }
    }
}

uint8_t csp_opcode_rtype(opcode_t op)
{
#if defined(__AVR__)
    return pgm_read_byte(&info_tab[op].rtype);
#else
    return info_tab[op].rtype;
#endif
}

uint8_t csp_opcode_arity(opcode_t op)
{
#if defined(__AVR__)
    return pgm_read_byte(&info_tab[op].arity);
#else
    return info_tab[op].arity;
#endif
}

const rochar* csp_opcode_name(opcode_t op)
{
    return (rochar*) RD_PTR(&info_tab[op].name);
}

int csp_set_error(csp_rt_t* st, csp_err_t err)
{
    // Don't overwrite a more specific error with generic SYNTAX
    if (st->ps.err == ERR_OK) {
	st->ps.err = err;
	return 1;
    }
    else if ((st->ps.err == ERR_SYNTAX) && (err != ERR_SYNTAX)) {
	st->ps.err = err;
	return 1;
    }
    return 0;
}

void csp_set_err_arg_int(csp_rt_t* st, int i, int ival)
{
    st->ps.err_args[i] = ival;
}

// Token string to temp area (grows down), set error with it
void csp_set_err_arg_tstr(csp_rt_t* st, int i, const tstr_t* str)
{
    if (st->ps.err_strp >= st->ps.strp + (uint32_t)str->len + 1) {
	st->ps.err_strp -= str->len + 1;
	memcpy(&st->str[st->ps.err_strp], str->ptr, str->len);
	st->str[st->ps.err_strp + str->len] = '\0';
	st->ps.err_args[i] = (uintptr_t)&st->str[st->ps.err_strp];
    }
}

// Decl name (already null-terminated in str[])
void csp_set_err_arg_ix(csp_rt_t* st, int i, index_t ix)
{
    st->ps.err_args[i] = (uintptr_t)decl_name(st, ix);
}

void csp_clr_error(csp_rt_t* st)
{
    st->ps.err = ERR_OK;
    st->ps.err_strp = MAX_STR_BUF;  // reset temp strings
}


// return pointer to the object/field value slot
value_t* csp_dio_slot(csp_rt_t* st, index_t ix, dio_t dir)
{
    int i = st_index(st, ix);
    return &st->dio[dir][i];
}

// return pointer to value pointer for input and output
int csp_dio_slots(csp_rt_t* st, index_t ix, value_t** iptr, value_t** optr)
{
    int i = st_index(st, ix);
    *iptr = &st->dio[DIN][i];
    *optr = &st->dio[DOUT][i];
    return 0;
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

static inline ivalue_t fsign(fvalue_t a)
{
    return (a < 0.0) ? -1 : (a ? 1 : 0);
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
    case V_TIMER: return csp_print_int(val.t.val);
    default: return csp_print_str("???");
    }
}

#ifdef DEBUG
void print_rentry(csp_rt_t* st, char* name, rentry_t* rp)
{
    if (debug) {
    printf("%s={", name);
    if (rp->X) printf("name=%s,", decl_name(st, rp->ix));
    printf("flags=");
    if (rp->I) printf("im ");
    if (rp->L) printf("ld ");
    if (rp->X) printf("ix ");
    printf(",vt=%s", csp_fmt_vtype(rp->vt));
    if (rp->L) printf(",reg=%d", rp->reg);
    if (rp->X) printf(",ix=0x%04x", rp->ix);
    if (rp->I) { printf(",val="); csp_print_value(st, rp->vt, rp->val); }
    printf("}");
    }
}
#endif

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
    fvalue_t arg = args[0].f;
    (void)st; (void)nargs;
    ret.f = (arg < 0) ? op_FNEG(arg) : arg;
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
    switch(type & 0xf) {
    case V_INTEGER: ret.i = isign(args[0].i); break;
    case V_FLOAT:   ret.i = fsign(args[0].f); break;
    }
    return ret;
}

// FIXME: compile  timeout(T) -> OP_TMO
static value_t fn_timeout(csp_rt_t* st,uint16_t type,
			  value_t* args, uint8_t nargs)
{
    value_t ret;
    index_t ty = args[0].u;  // timer
    value_t* vptr = csp_dio_slot(st, ty, DIN);
    ret.i = BOOL(vptr->t.fired);
    return ret;
}

//  FIXME: if not running?
static value_t fn_elapsed(csp_rt_t* st,uint16_t type,
			  value_t* args, uint8_t nargs)
{
    value_t ret;
    index_t ty = args[0].u; // timer
    index_t tx = ty+1;
    uint32_t td = csp_time_ms() - csp_uvalue(st, tx);
    ret.u = td;
    return ret;
}

//  FIXME: if not running?
static value_t fn_progress(csp_rt_t* st,uint16_t type,
			   value_t* args, uint8_t nargs)
{
    value_t ret;
    index_t ty = args[0].u; // timer
    index_t tx = ty+1;      // start time
    uint32_t td = csp_time_ms() - csp_uvalue(st, tx);
    value_t* iptr = csp_dio_slot(st, ty, DIN);    
    uint32_t period = iptr->t.period;

    if (td >= period)
	ret.f = op_CVTIF(1);
    else
	ret.f = op_FDIV(op_CVTIF(td), op_CVTIF(period));
    return ret;
}

// FIXME: compile  changed(X) -> OP_CHG
static value_t fn_changed(csp_rt_t* st,uint16_t type,
			  value_t* args, uint8_t nargs)
{
    value_t ret;
    index_t ty = args[0].u;
    int i = st_index(st, ty);
    ret.i = BOOL(bitset_tst(st->dset, i));
    return ret;
}

static value_t fn_rising(csp_rt_t* st,uint16_t type,
			 value_t* args, uint8_t nargs)
{
    value_t ret;
    index_t ty = args[0].u;
    value_t* iptr;    
    value_t* optr;

    csp_dio_slots(st, ty, &iptr, &optr);
    ret.i = !(optr->d.val & 1) && (iptr->d.val & 1);
    return ret;
}

static value_t fn_falling(csp_rt_t* st,uint16_t type,
			  value_t* args, uint8_t nargs)
{
    value_t ret;
    index_t ty = args[0].u;
    value_t* iptr;    
    value_t* optr;

    csp_dio_slots(st, ty, &iptr, &optr);
    ret.i = (optr->d.val & 1) && !(iptr->d.val & 1);
    return ret;
}

static value_t fn_print(csp_rt_t* st, uint16_t type,
			 value_t* args,uint8_t nargs)
{
    value_t ret;
    ret.i = csp_print_value(st, type & 0xf, args[0]);
    return ret;
}

static value_t fn_println(csp_rt_t* st, uint16_t type,
			  value_t* args,uint8_t nargs)
{
    value_t ret = fn_print(st, type, args, nargs);
    ret.i += csp_println();
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

// set latch state and return previous value
static value_t fn_latch(csp_rt_t* st,uint16_t type,value_t* args, uint8_t nargs)
{
    value_t ret;
    (void)args; (void)nargs;
    ret.i = csp_set_latch(st, args[0].i);
    return ret;
}

static rochar s_min[] RODATA = "min";
static rochar s_max[] RODATA = "max";
static rochar s_abs[] RODATA = "abs";
static rochar s_fabs[] RODATA = "fabs";
static rochar s_sign[] RODATA = "sign";
static rochar s_clip[] RODATA = "clip";
static rochar s_timeout[] RODATA = "timeout";
static rochar s_elapsed[] RODATA = "elapsed";
static rochar s_progress[] RODATA = "progress";
static rochar s_changed[] RODATA = "changed";
static rochar s_rising[] RODATA = "rising";
static rochar s_falling[] RODATA = "falling";

static rochar s_print[] RODATA = "print";
static rochar s_println[] RODATA = "println";
static rochar s_tick[] RODATA = "tick";
static rochar s_cycle[] RODATA = "cycle";
static rochar s_latch[] RODATA = "latch";

#define CSP_FUNC_ENT(str, a, p, rt, args, f)	\
    {.name=(str),.namelen=sizeof((str))-1,.arity=(a),.pure=(p),	\
	    .rtype=(rt),.argtypes=(args),.fn=(f)}

// Built-in function table
// { name, namelen, nargs, rtype, argtypes, fn }
const csp_func_t csp_builtin_funcs[] RODATA = {
    // match functions
    CSP_FUNC_ENT(s_min,     2, 1, V_INTEGER, MAKE_TYPE2(V_INTEGER,V_INTEGER), fn_min ),
    CSP_FUNC_ENT(s_max,     2, 1, V_INTEGER, MAKE_TYPE2(V_INTEGER,V_INTEGER), fn_max ),
    CSP_FUNC_ENT(s_abs,     1, 1, V_INTEGER, MAKE_TYPE1(V_INTEGER), fn_abs ),
    CSP_FUNC_ENT(s_fabs,    1, 1, V_FLOAT,   MAKE_TYPE1(V_FLOAT),   fn_fabs ),
    CSP_FUNC_ENT(s_sign,    1, 1, V_INTEGER, MAKE_TYPE1(V_NUMBER),  fn_sign ),
    CSP_FUNC_ENT(s_clip,    3, 1, V_INTEGER, MAKE_TYPE3(V_INTEGER,V_INTEGER,V_INTEGER), fn_clip),
    // timer functions
    CSP_FUNC_ENT(s_timeout, 1, 0, V_INTEGER, MAKE_TYPE1(V_TIMER), fn_timeout),
    CSP_FUNC_ENT(s_elapsed,  1, 0, V_INTEGER, MAKE_TYPE1(V_TIMER), fn_elapsed),
    CSP_FUNC_ENT(s_progress, 1, 0, V_FLOAT, MAKE_TYPE1(V_TIMER), fn_progress),
    // variable changed detection
    CSP_FUNC_ENT(s_changed, 1, 0, V_INTEGER, MAKE_TYPE1(V_INDEX), fn_changed),
    CSP_FUNC_ENT(s_rising,  1, 0, V_INTEGER, MAKE_TYPE1(V_DIGITAL), fn_rising),
    CSP_FUNC_ENT(s_falling, 1, 0, V_INTEGER, MAKE_TYPE1(V_DIGITAL), fn_falling),
    
    CSP_FUNC_ENT(s_print,   1, 0, V_INTEGER, MAKE_TYPE1(V_ANY),  fn_print),
    CSP_FUNC_ENT(s_println, 1, 0, V_INTEGER, MAKE_TYPE1(V_ANY),  fn_println),
    CSP_FUNC_ENT(s_tick,    0, 0, V_INTEGER, MAKE_TYPE0(),       fn_tick),
    CSP_FUNC_ENT(s_cycle,   0, 0, V_INTEGER, MAKE_TYPE0(),       fn_cycle),
    CSP_FUNC_ENT(s_latch,   1, 0, V_INTEGER, MAKE_TYPE1(V_INTEGER), fn_latch),
};

const uint8_t csp_num_builtin_funcs = sizeof(csp_builtin_funcs)/sizeof(csp_builtin_funcs[0]);

static uint8_t func_arity(const csp_func_t* fn, int i)
{
    return RD_BYTE(&fn[i].arity);
}

// is function "pure"
static uint8_t func_pure(const csp_func_t* fn, int i)
{
    return RD_BYTE(&fn[i].pure);
}

static uint8_t func_namelen(const csp_func_t* fn,int i)
{
    return RD_BYTE(&fn[i].namelen);
}

static csp_func_fn func_fn(const csp_func_t* fn, int i)
{
    return (csp_func_fn) RD_PTR(&fn[i].fn);
}

static uint8_t fn_type(const csp_func_t* fn, int j)
{
    uint16_t argtypes = RD_WORD(&fn->argtypes);
    return (argtypes >> 4*j) & 0xf;
}

// match function template this code assumes type coerce int->flt
// flt->int. the goal is to match BEST? function to use
// return 0 on match
// return argument number 1...n on mismatch
int csp_match_args(csp_rt_t* st, const csp_func_t* fn, int arity, rentry_t* rarg)
{
    int j;
    for (j = 0; j < arity; j++) {
	rentry_t arg = rarg[j];
	vtype_t argvt = arg.vt;
	uint8_t ftype = fn_type(fn, j);
	switch(ftype) {
	case V_ANY:
	    break;
	case V_NUMBER:
	    if (argvt == V_INTEGER) break;
	    if (argvt == V_FLOAT) break;
	    goto mismatch;
	case V_INTEGER:
	    if (argvt == V_INTEGER) break;
	    if (argvt == V_FLOAT) break;    // coerce!
	    goto mismatch;
	case V_FLOAT:
	    if (argvt == V_FLOAT) break;
	    if (argvt == V_INTEGER) break;  // coerce!
	    goto mismatch;
	case V_STRING:
	    if (argvt == V_STRING) break;
	    goto mismatch;
	case V_INDEX:
	    if (arg.X) break;
	    goto mismatch;
	case V_TIMER:
	    if (arg.X && (st->decl[INDEX(arg.ix)].type == DECL_TIMER)) break;
	    goto mismatch;
	case V_DIGITAL:
	    if (arg.X && (st->decl[INDEX(arg.ix)].type == DECL_DIGITAL)) break;
	    goto mismatch;
	case V_ANALOG:
	    if (arg.X && (st->decl[INDEX(arg.ix)].type == DECL_ANALOG)) break;
	    goto mismatch;
	case V_CAN:
	    if (arg.X && (st->decl[INDEX(arg.ix)].type == DECL_CAN)) break;
	    goto mismatch;
	default:
	    goto mismatch;
	}
    }
    return 0;
mismatch:
    return j+1;
}

static int csp_match_fn(csp_rt_t* st,
			const csp_func_t* fn, int num,
			const tstr_t* name,
			uint8_t arity, rentry_t* rarg)
{
    int i;
    int a, f = -1;
    for (i = 0; i < num; i++) {
	uint8_t roarity = func_arity(fn, i);
	uint8_t ronamelen = func_namelen(fn, i);
	if ((roarity == arity) && (ronamelen == name->len)) {
	    const char* roname = RD_PTR(&fn[i].name);
	    if (MEMCMP_RD(name->ptr, roname, name->len) == 0) {
		int j;
		if ((j=csp_match_args(st, &fn[i], arity, rarg)) == 0) // ok
		    return i;
		f = i;  // last name match
		a = j;  // and argument poistion that failed
	    }
	}
    }
    if (f >= 0) {
	if (csp_set_error(st, ERR_FUNCTION_ARGUMENT_TYPE_MISMATCH)) {
	    csp_set_err_arg_tstr(st, 0, name);
	    csp_set_err_arg_int(st, 1, arity);
	    csp_set_err_arg_int(st, 2, a);
	}
    }
    return -1;
}

const csp_func_t* csp_match_func(csp_rt_t* st,
				 const tstr_t* name,
				 uint8_t arity, rentry_t* rarg,
				 int* is_user, int* func_idx)
{
    int idx;

    if (st->ufuncs) {
	if ((idx = csp_match_fn(st, st->ufuncs, st->num_ufuncs,
				name, arity, rarg)) >= 0) {
	    *is_user = 1;
	    *func_idx = idx;
	    return &st->ufuncs[idx];
	}
    }
    if ((idx = csp_match_fn(st, csp_builtin_funcs, csp_num_builtin_funcs,
			    name, arity, rarg)) >= 0) {
	*is_user = 0;
	*func_idx = idx;
	return &csp_builtin_funcs[idx];
    }
    if (csp_set_error(st, ERR_FUNCTION_DOES_NOT_EXIST)) {
	csp_set_err_arg_tstr(st, 0, name);
	csp_set_err_arg_int(st, 1, arity);
    }
    return NULL;
}


int find_op_entry(const char* name, int namelen)
{
    int i = 0;
    uint8_t n = sizeof(op_table)/sizeof(op_table[0])-1;
    for (i = 0; i < n; i++) {
	uint8_t ronamelen = RD_BYTE(&op_table[i].namelen);
	if (ronamelen == namelen) {
	    const char* roname = RD_PTR(&op_table[i].name);
	    if (MEMCMP_RD(name, roname, ronamelen) == 0)
		return i;
	}
    }
    return -1;
}

static inline int8_t op_table_tok(int i)
{
    return RD_BYTE(&op_table[i].tok);
}

static inline int8_t op_table_arity(int i)
{
    return RD_BYTE(&op_table[i].arity);
}

static inline int8_t op_table_code(int i)
{
    return RD_BYTE(&op_table[i].code);
}

static inline int8_t op_table_prec(int i)
{
    return RD_BYTE(&op_table[i].prec);
}

static inline int8_t op_table_assoc(int i)
{
    return RD_BYTE(&op_table[i].assoc);
}


// enq all rules that depend on declaration x
NOINLINE void csp_enq_elist(csp_rt_t* st, index_t x)
{
#if defined(SUPPORT_REACTIVE) && (SUPPORT_REACTIVE==1)
    int i;
    index_t ix = INDEX(x);
    index_t base = st->ofs[ix];
    uint8_t obj = OBJ(x);
    for (i = 0; i < st->idg[ix]; i++) {
	index_t p = st->edg[base+i];  // parent node (instruction index)
	csp_enq(st, obj, p);
    }
#endif
}

NOINLINE void csp_dio_set_pin_part(csp_rt_t* st, value_t* vslot,
				   vtype_t vt, value_t v)
{
    switch(vt) {
    case V_DIGITAL: vslot->d.pin = v.i; break;
    case V_ANALOG:  vslot->a.pin = v.i; break;
    default: break;
    }
}

NOINLINE void csp_dio_set_port_part(csp_rt_t* st, value_t* vslot,
				    vtype_t vt, value_t v)
{
    switch(vt) {
    case V_DIGITAL: vslot->d.port = v.i; break;
    case V_ANALOG:  vslot->a.port = v.i; break;
    default: break;
    }
}

NOINLINE void csp_dio_set_dir_part(csp_rt_t* st, value_t* vslot,
				   vtype_t vt, value_t v)
{
    switch(vt) {
    case V_DIGITAL: vslot->d.dir = v.i; break;
    case V_ANALOG:  vslot->a.dir = v.i; break;
    default: break;
    }
}

NOINLINE void csp_dio_set_val_part(csp_rt_t* st, value_t* vslot,
				   vtype_t vt, value_t v)
{
    switch(vt) {
    case V_TIMER:   vslot->t.val = v.i; break;
    case V_DIGITAL: vslot->d.val = v.i; break;
    case V_ANALOG:  vslot->a.val = v.i; break;
    default: *vslot = v; break;
    }
}

NOINLINE void csp_dio_get_val_part(csp_rt_t* st, value_t* vslot,
				   vtype_t vt, value_t* vp)
{
    switch(vt) {
    case V_TIMER:   vp->i = vslot->t.val; break;
    case V_DIGITAL: vp->i = vslot->d.val & 1; break;
    case V_ANALOG:  vp->i = vslot->a.val; break;
    default: *vp = *vslot; break;
    }
}

// Set value part in dio (config data & value)
NOINLINE void csp_dio_set_part(csp_rt_t* st, index_t ix, value_t v,
			       csp_part_t part, dio_t dir)
{
    value_t* vslot = csp_dio_slot(st, ix, dir);
    vtype_t vt = st->decl[INDEX(ix)].vt;
    switch(part) {
    case PART_VAL:
	csp_dio_set_val_part(st, vslot, vt, v);
	break;
    case PART_PIN:
	csp_dio_set_pin_part(st, vslot, vt, v);
	break;
    case PART_PORT:
	csp_dio_set_port_part(st, vslot, vt, v);
	break;
    case PART_DIR:      // V_DIGITAL/V_ANALOG/V_CAN
	csp_dio_set_dir_part(st, vslot, vt, v);	
    case PART_PWM:      // V_ANALOG
	if (vt == V_ANALOG)
	    vslot->a.pwm = v.i;
	break;
    case PART_ENDIAN:   // V_ANALOG/V_CAN
	if (vt == V_ANALOG)
	    vslot->a.endian = v.i;
	break;
    case PART_PULLUP:   // V_DIGITAL
	if (vt == V_DIGITAL)
	    vslot->d.pullup = v.i;
	break;	
    case PART_PULLDOWN: // V_DIGITAL
	if (vt == V_DIGITAL)
	    vslot->d.pulldown = v.i;	
	break;
    case PART_PERIOD:   // V_TIMER
	if (vt == V_TIMER)
	    vslot->t.period = v.i;
	break;
    case PART_FIRED:    // V_TIMER
	if (vt == V_TIMER)
	    vslot->t.fired = v.i;
	break;	
    default:
	break;
    }
}

NOINLINE void csp_dio_set(csp_rt_t* st, index_t ix, value_t v, dio_t dir)
{
    value_t* vslot = csp_dio_slot(st, ix, dir);
    vtype_t vt = st->decl[INDEX(ix)].vt;    
    csp_dio_set_val_part(st, vslot, vt, v);
}

NOINLINE void csp_dio_get(csp_rt_t* st, index_t ix, value_t* vp, dio_t dir)
{
    value_t* vslot = csp_dio_slot(st, ix, dir);
    vtype_t vt = st->decl[INDEX(ix)].vt;
    csp_dio_get_val_part(st, vslot, vt, vp);
}

NOINLINE void csp_set_value(csp_rt_t* st, index_t n, value_t v)
{
    value_t cv;
    csp_dio_get(st, n, &cv, DOUT);
    if (v.u != cv.u) {
	int i = st_index(st, n);
	bitset_set(st->dset, i);
	st->anyd = CSP_TRUE;
#if defined(SUPPORT_REACTIVE) && (SUPPORT_REACTIVE==1)
	if (st->reactive)
	    csp_enq_elist(st,n);
#endif
	csp_dio_set(st, n, v, DOUT);
	st->update++;
    }
}

NOINLINE value_t csp_value(csp_rt_t* st, index_t n)
{
    value_t cv;
    csp_dio_get(st, n, &cv, DIN);
    return cv;
}

NOINLINE void csp_set_ivalue(csp_rt_t* st, index_t n, ivalue_t v)
{
    value_t vv;
    vv.i = v;
    csp_set_value(st, n, vv);
}

NOINLINE void csp_set_fvalue(csp_rt_t* st, index_t n, fvalue_t v)
{
    value_t vv;
    vv.f = v;
    csp_set_value(st, n, vv);
}

// eval until NEXT!
int csp_eval_rule(csp_rt_t* st, int n)
{
    opcode_t op;
#if defined(USE_STATISTICS) && (USE_STATISTICS==1)
    st->num_eval_rule++;
#endif

again:
#if defined(USE_STATISTICS) && (USE_STATISTICS==1)
    st->num_eval0++;
#endif
    op = st->instr[n].op;
    switch(op) {
    case OP_NOP:
	break;
    case OP_LD:
	st->reg[st->instr[n].m.x] = csp_value(st, st->instr[n].m.mem);
	break;
    case OP_STIMP:  // same as ST, but marks reactive assignment
    case OP_ST:
	csp_set_value(st, st->instr[n].m.mem, st->reg[st->instr[n].m.x]);
	break;
    case OP_STP:
	csp_dio_set_part(st, st->instr[n].m.mem, st->reg[st->instr[n].m.x],
			 st->instr[n].m.part, DOUT);
	break;
    case OP_CHG: {  // r |= dset[ix]
	int i = st_index(st, st->instr[n].m.mem);
	st->reg[st->instr[n].m.x].i |= bitset_tst(st->dset, i) ? 1 : 0;
	break;
    }
    case OP_LI:
	st->reg[st->instr[n].i.x].i = st->instr[n].i.imm;  // sign extend
	break;
    case OP_LIU:
	st->reg[st->instr[n].i.x].u = (uint16_t)st->instr[n].i.imm;  // zero extend
	break;
    case OP_LIH:
	st->reg[st->instr[n].i.x].u |= ((uint32_t)(uint16_t)st->instr[n].i.imm) << 16;
	break;
    case OP_ARG:
	st->arg[st->instr[n].i.imm] = st->reg[st->instr[n].i.x];
	break;
    case OP_RULE:
	if (st->reg[st->instr[n].r.cnd].i)
	    n = n+1;
	else
	    n = n+st->instr[n].r.nxt;  // relative jump
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
	    st->offs[CURRENT] = st->offs[st->cur];  // setup locals
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
	uint8_t arity;
	csp_func_fn fn = NULL;

	// Get function pointer
	if (st->instr[n].f.usr) {
	    if (st->ufuncs && (idx < st->num_ufuncs)) {
		arity = func_arity(st->ufuncs, idx);
		fn    = func_fn(st->ufuncs, idx);
	    }
	}
	else {
	    if (idx < csp_num_builtin_funcs) {
		arity = func_arity(csp_builtin_funcs, idx);
		fn    = func_fn(csp_builtin_funcs, idx);
	    }
	}
	if (fn) {
	    value_t val = fn(st, st->instr[n].f.avt, st->arg, arity);
	    st->reg[st->instr[n].f.x] = val;
	}
	break;
    }
    default: {
	value_t xv, yv, zv;

	switch(csp_opcode_arity(op)) {
	case 0:
	    xv = eval0(op);
	    break;
	case 1:
	    yv = st->reg[st->instr[n].a.y];
	    xv = eval1(op, yv);
	    break;
	case 2:
	    yv = st->reg[st->instr[n].a.y];
	    zv = st->reg[st->instr[n].a.z];
	    xv = eval2(op, yv, zv); //eval_tab2[op](yv,zv);
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
	tmp = st->dio[DIN];
	st->dio[DIN] = st->dio[DOUT];
	st->dio[DOUT] = tmp;
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
    while(n < st->ps.nn) {
	n = csp_eval_rule(st, n);
	x = n;
    }
    return x;
}

// run queue until cycle boundary
index_t csp_react(csp_rt_t* st)
{
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
NOINLINE static index_t lookup_decl_in(csp_rt_t* st, const tstr_t* name,
				       int start, int stop)
{
    int i = start;

    while(i < stop) {
	int pos = st->decl[i].name;
	if (pos > 0) {
	    int len = st->str[pos-1];  // FIXME: RODATA
	    index_t ix = MAKE_INDEX(0,i);
	    if ((len == name->len) &&
		(memcmp(decl_name(st, ix),name->ptr, name->len)==0)) {
		return ix;
	    }
	}
	if (st->decl[i].type == DECL_MODULE) // skip module def
	    i += (st->decl[i].md.n+1); // skip elements and END
	i++;
    }
    return BAD_INDEX;
}

NOINLINE static index_t lookup_decl(csp_rt_t* st, const tstr_t* name)
{
    int start = (st->mdef != BAD_INDEX) ? INDEX(st->mdef)+1 : 0;
    return lookup_decl_in(st, name, start, st->ps.nd);
}

NOINLINE index_t lookup_const(csp_rt_t* st, vtype_t vt, value_t v)
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

NOINLINE index_t lookup_string_const(csp_rt_t* st, char* str, int slen)
{
    index_t i;
    for (i = 0; i < st->ps.nd; i++) {
	if (IS_CONST(st, i) && (st->decl[i].vt == V_STRING)) {
	    sindex_t si = st->decl[i].cn.init.s;
	    int len = st->str[si-1];  // length is in byte before spos
	    if ((len == slen) &&
		(memcmp(str, &st->str[st->decl[i].cn.init.s], slen) == 0))
		return MAKE_INDEX(0,i);
	}
    }
    return BAD_INDEX;
}

// each string is installed like
//  [3] 'a' 'b' 'c' '\0'
// length byte characters terminated with 0
// position return is pos efter length byte
NOINLINE int new_string(csp_rt_t* st, char* name, int len)
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

// Find a string in string buffer
NOINLINE int lookup_string(csp_rt_t* st, char* name, int name_len)
{
    int pos = 1;  // search from pos=1 in str buf
    while(pos < st->ps.strp) {
	int len = st->str[pos];
	if (len == name_len) {
	    if (memcmp(&st->str[pos+1],name,name_len) == 0)
		return pos+1;
	}
	pos += (len+2);  // length byte and \0
    }
    return -1;
}

NOINLINE index_t next_decl_index(csp_rt_t* st)
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

NOINLINE index_t csp_new_decl(csp_rt_t* st, const tstr_t* name, decl_t type)
{
    index_t i;
    int pos;

    if ((i = next_decl_index(st)) == BAD_INDEX)
	return BAD_INDEX;
    pos = 0;
    if (name != NULL) {
	if ((pos = new_string(st, name->ptr, name->len)) < 0)
	    return BAD_INDEX;
    }
    st->decl[INDEX(i)].type = type;
    st->decl[INDEX(i)].name = pos;
    st->decl[INDEX(i)].vt = V_INTEGER;
    return i;
}

// new uniq declaration
NOINLINE index_t csp_new_udecl(csp_rt_t* st, const tstr_t* name, decl_t type)
{
    index_t ix;
    
    if ((ix = lookup_decl(st, name)) != BAD_INDEX) {
	if (csp_set_error(st, ERR_ALREADY_DEFINED)) {
	    tstr_t typ = { .ptr = "name", .len = 4 };
	    if (st->decl[ix].type == type) typ = decl_type_name(type);
	    csp_set_err_arg_tstr(st, 0, &typ);
	    csp_set_err_arg_tstr(st, 1, name);
	}
	return BAD_INDEX;
    }
    return csp_new_decl(st, name, type);
}

NOINLINE index_t new_signed_const(csp_rt_t* st, ivalue_t v)
{
    index_t ix;
    int i;
    const tstr_t empty = { .ptr = NULL, .len = 0};
    if ((ix = csp_new_decl(st,&empty,DECL_CONSTANT)) == BAD_INDEX)
	return BAD_INDEX;
    i = INDEX(ix);
    st->decl[i].res = MAKE_RES(8*sizeof(ivalue_t));
    st->decl[i].vt = V_INTEGER;
    st->decl[i].cn.init.i = v;
    return ix;
}

NOINLINE index_t new_float_const(csp_rt_t* st, fvalue_t v)
{
    index_t ix;
    int i;
    const tstr_t empty = { .ptr = NULL, .len = 0};    
    if ((ix = csp_new_decl(st,&empty,DECL_CONSTANT)) == BAD_INDEX)
	return BAD_INDEX;
    i = INDEX(ix);
    st->decl[i].res = MAKE_RES(8*sizeof(fvalue_t));
    st->decl[i].vt = V_FLOAT;
    st->decl[i].cn.init.f = v;
    return ix;
}

NOINLINE index_t new_string_const(csp_rt_t* st, char* str, int len)
{
    index_t ix;
    int pos, i;
    const tstr_t empty = { .ptr = NULL, .len = 0};
    if ((ix = csp_new_decl(st,&empty,DECL_CONSTANT)) == BAD_INDEX)
	return BAD_INDEX;
    if ((pos = new_string(st, str, len)) < 0)
	return BAD_INDEX;
    i = INDEX(ix);
    st->decl[i].res = MAKE_RES(STRING_BITS);
    st->decl[i].vt = V_STRING;
    st->decl[i].cn.init.s = pos;
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

int csp_new_next(csp_rt_t* st, int r)
{
    int i;
    if ((i = csp_new_instr(st, OP_NEXT)) >= 0) {
	st->instr[i].x.x = r;
    }
    return i;
}

NOINLINE static int asm_mem_part(csp_rt_t* st, opcode_t op, reg_t x,
				 index_t mem, csp_part_t part)
{
    int i;
    if ((i = csp_new_instr(st, op)) >= 0) {
	st->instr[i].m.part = part;
	st->instr[i].m.x = x;
	st->instr[i].m.mem = mem;
    }
    return i;
}

NOINLINE static int asm_mem(csp_rt_t* st, opcode_t op, reg_t x, index_t mem)
{
    return asm_mem_part(st, op, x, mem, PART_VAL);
}

NOINLINE static int asm_imm(csp_rt_t* st, opcode_t op, reg_t x, int16_t imm)
{
    int i;
    if ((i = csp_new_instr(st, op)) >= 0) {
	st->instr[i].i.x = x;
	st->instr[i].i.imm = imm;
    }
    return i;
}

NOINLINE static int asm_LI(csp_rt_t* st, reg_t x, int16_t imm)
{
    return asm_imm(st, OP_LI, x, imm);
}

NOINLINE static int asm_LIU(csp_rt_t* st, reg_t x, uint16_t imm)
{
    return asm_imm(st, OP_LIU, x, (int16_t)imm);
}

NOINLINE static int asm_LIH(csp_rt_t* st, reg_t x, uint16_t imm)
{
    return asm_imm(st, OP_LIH, x, (int16_t)imm);
}

// Smart load: choose LI, LIU, or LIU+LIH based on value
NOINLINE static int csp_load_int(csp_rt_t* st, reg_t x, ivalue_t val)
{
    if ((val >= -32768) && (val <= 32767)) {
	return asm_LI(st, x, (int16_t)val);
    }
    else {
	uint32_t uval = (uint32_t)val;
	if (asm_LIU(st, x, (uint16_t)(uval & 0xFFFF)) < 0)
	    return -1;
	if (uval > 0xFFFF) {
	    if (asm_LIH(st, x, (uint16_t)(uval >> 16)) < 0)
		return -1;
	}
	return 0;
    }
}

NOINLINE static int csp_load_uint(csp_rt_t* st, reg_t x, uvalue_t val)
{
    if (val <= 32767) {
	return asm_LI(st, x, (int16_t)val);
    }
    else if (val <= 0xFFFF) {
	return asm_LIU(st, x, (uint16_t)val);
    }
    else {
	if (asm_LIU(st, x, (uint16_t)(val & 0xFFFF)) < 0)
	    return -1;
	return asm_LIH(st, x, (uint16_t)(val >> 16));
    }
}

NOINLINE static int csp_load_float(csp_rt_t* st, reg_t x, fvalue_t val)
{
#if FVALUE_IS_FIXPOINT
    // Fixpoint is just an int32_t, load as signed
    return csp_load_int(st, x, (ivalue_t)val);
#else
    union { float f; uint32_t u; } v;
    v.f = val;
    if (v.u == 0) {
	return asm_LI(st, x, 0);  // 0.0
    }
    if (asm_LIu(st, x, (uint16_t)(v.u & 0xFFFF)) < 0)
	return -1;
    return asm_LIH(st, x, (uint16_t)(v.u >> 16));
#endif
}

static int asm_ARG(csp_rt_t* st, reg_t x, int16_t i)
{
    return asm_imm(st, OP_ARG, x, i);
}


NOINLINE static int asm_alu(csp_rt_t* st, opcode_t op,reg_t x, reg_t y, reg_t z)
{
    int i;
    if ((i = csp_new_instr(st, op)) >= 0) {
	st->instr[i].a.x = x;
	st->instr[i].a.y = y;
	st->instr[i].a.z = z;
    }
    return i;
}

NOINLINE int csp_new_rule(csp_rt_t* st, reg_t cnd, int nxt)
{
    int i;
    if ((i = csp_new_instr(st, OP_RULE)) >= 0) {
	st->instr[i].r.cnd = cnd;
	st->instr[i].r.nxt = nxt;
    }
    return i;
}

NOINLINE static int asm_call(csp_rt_t* st, reg_t x, int func_idx, int is_user,
			     uint16_t argcode)
{
    int i;
    if ((i = csp_new_instr(st, OP_CALL)) >= 0) {
	st->instr[i].f.x   = x;
	st->instr[i].f.idx = func_idx;
	st->instr[i].f.usr = is_user;
	st->instr[i].f.avt = argcode;
    }
    return i;
}

NOINLINE static int asm_enter(csp_rt_t* st, int n, index_t mx)
{
    int i;
    if ((i = csp_new_instr(st, OP_ENTER)) >= 0) {
	st->instr[i].e.num = n;
	st->instr[i].e.mx = mx;
    }
    return i;
}

NOINLINE static int asm_leave(csp_rt_t* st, int n, index_t mx)
{
    int i;
    if ((i = csp_new_instr(st, OP_LEAVE)) >= 0) {
	st->instr[i].v.num = n;
	st->instr[i].v.mx = mx;
    }
    return i;
}

NOINLINE static int asm_new(csp_rt_t* st, unsigned ent, index_t obj)
{
    int i;
    if ((i = csp_new_instr(st, OP_NEW)) >= 0) {
	st->instr[i].n.ent = ent;
	st->instr[i].n.obj = obj;
    }
    return i;
}

static int asm_bop(csp_rt_t* st, opcode_t op, index_t x ,index_t y, index_t z)
{
    return asm_alu(st, op, x, y, z);
}

static int asm_uop(csp_rt_t* st, opcode_t op, index_t x, index_t y)
{
    return asm_alu(st, op, x, y, 0);
}

static int asm_CVTIF(csp_rt_t* st, index_t x, index_t y)
{
    return asm_uop(st, OP_CVTIF, x, y);
}

static int asm_CVTFI(csp_rt_t* st, index_t x, index_t y)
{
    return asm_uop(st, OP_CVTFI, x, y);
}

static int asm_MOV(csp_rt_t* st, reg_t x, reg_t y)
{
    return asm_uop(st, OP_MOV, x, y);
}

static int asm_AND(csp_rt_t* st, reg_t x, reg_t y, reg_t z)
{
    return asm_bop(st, OP_AND, x, y, z);
}

// compare equal ==
static int asm_EQEQ(csp_rt_t* st, reg_t x, reg_t y, reg_t z)
{
    return asm_bop(st, OP_EQEQ, x, y, z);
}

/*
static int asm_OR(csp_rt_t* st, reg_t x, reg_t y, reg_t z)
{
    return asm_bop(st, OP_OR, x, y, z);
}
*/



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
	case OP_CHG:
	    if (current_rule >= 0) {
		index_t mem = INDEX(st->instr[i].m.mem);
		if (mem < st->ps.nd) {
		    st->idg[mem]++;
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
    for (i = 0; i < st->ps.nd; i++) {
	st->ofs[i+1] = st->ofs[i] + st->idg[i];
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
	case OP_CHG:
	    if (current_rule >= 0) {
		index_t mem = INDEX(st->instr[i].m.mem);
		if (mem < st->ps.nd &&
		    (wr[mem] == st->ofs[mem] || st->edg[wr[mem]-1] != current_rule))
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
    // Update idg to reflect actual (deduplicated) counts
    for (i = 0; i < st->ps.nd; i++)
	st->idg[i] = wr[i] - st->ofs[i];
#endif
}

NOINLINE static int dec(int c)
{
    if ((c >= '0') && (c <= '9'))
	return (c - '0');
    return 0;
}

NOINLINE static int hex(int c)
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
#define SYM(x,p,l) do { \
	tok = (x); \
	val.str.ptr=(p);	    \
	val.str.len=(l); \
	goto done;	 \
    } while(0)
#define TOK_INT(y) do { tok = INT; val.val.i = (y); goto done; } while(0)
#define TOK_FLT(y) do { tok = FLT; val.val.f = (y); goto done; } while(0)

NOINLINE static int csp_next_token(char* str, token_t* tp)
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
	case '-': str++; TOK(RIMP);
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
	if (ISDIGIT(c))
	    goto number;
	else if (ISALPHA(c)) {
	    char *name = str-1;
	    int len = 1;
	    int i;
	    while (ISALPHA(*str) || ISDIGIT(*str) || (*str == '_') ) {
		str++;
		len++;
	    }
	    if (len > MAX_NAME_LEN)
		return -1; // fixme set error code
	    if ((i = find_op_entry(name,len)) >= 0)
		TOK(op_table_tok(i));
	    SYM(WORD, name, len);
	}
	return -1;
    number:
	if ((c == '0') && (*str == 'x')) {
	    ivalue_t v = 0;
	    str++;
	    while(ISXDIGIT(*str)) {
		v = v*16 + hex(*str++);
	    }
	    TOK_INT(v*sign);
	}
	if (ISDIGIT(c)) {
	    ivalue_t v = dec(c);
	    while(ISDIGIT(*str)) {
		v = v*10 + dec(*str++);
	    }
	    // parse simple fraction for now
	    if ((str[0] == '.') && ISDIGIT(str[1])) {
#if FVALUE_IS_FIXPOINT
		// Parse as Q16.16 fixpoint
		fvalue_t frac;
		uint32_t denom = 1;
		uint32_t numer = 0;
		str++;
		while(ISDIGIT(*str)) {
		    numer = numer*10 + dec(*str++);
		    denom *= 10;
		}
		frac = (int32_t)(((uint64_t)numer<<FIX_SHIFT) / denom);
		fvalue_t result = FIX_FROM_INT(v) + frac;
		TOK_FLT(sign >= 0 ? result : -result);
#else
		float b = 0.1;
		float f = 0.0;
		str++;
		while(ISDIGIT(*str)) {
		    f = f + (b*dec(*str++));
		    b /= 10.0;
		}
		f += v;
		TOK_FLT(f*sign);
#endif
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
NOINLINE int csp_scan_line(char* str, token_t* tv, size_t* num_toks)
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

NOINLINE static void alloc_init(reg_allocator_t* ap)
{
    int i;
    for (i = 0; i < MAX_REGS; i++) {
	ap->free_regs[i] = i;
	ap->rmap[i] = BAD_INDEX;
    }
    ap->top = 0;
}

NOINLINE static int alloc_reg(csp_rt_t* st)
{
    reg_allocator_t* ap;
    if ((ap = st->ap) != NULL) {
	int r = ap->free_regs[ap->top++];
	ap->rmap[r] = BAD_INDEX;
	return r;
    }
    return 0;
}

NOINLINE static void free_reg(csp_rt_t* st, int r)
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

// load immedate value.
NOINLINE int csp_load_value(csp_rt_t* st, reg_t x, vtype_t vt, value_t val)
{
    switch(vt) {
    case V_INDEX:
    case V_TIMER:
    case V_DIGITAL:
    case V_ANALOG:
    case V_CAN:
	return asm_LI(st, x, val.i);
    case V_INTEGER:
	return csp_load_int(st, x, val.i);
    case V_UNSIGNED:
	return csp_load_uint(st, x, val.u);
    case V_FLOAT:
	return csp_load_float(st, x, val.f);
    case V_STRING:
	return asm_LI(st, x, val.s);  // load string index
    default:
	return -1;
    }
}

// Add unique variable to var list (for <- parsing)
NOINLINE static void add_var(csp_rt_t* st, index_t ix)
{
    if (st->rimp) {  // only when in RHS in expression x <- a+b+c
	int i;
	for (i = 0; i < st->nvar; i++)
	    if (st->var[i] == ix) return;  // already in list
	if (st->nvar < MAX_VARREFS)
	    st->var[st->nvar++] = ix;
    }
}

// Map declaration (variable/constant/digital...)
NOINLINE int map_reg(csp_rt_t* st, index_t ix)
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
	    value_t val = st->decl[INDEX(ix)].cn.init;
	    vtype_t vt = st->decl[INDEX(ix)].vt;
	    if (csp_load_value(st, dst, vt, val) < 0)
		return -1;
	    return dst;
	}
	// generate LD instruction for variables, track for <- rules
	if (asm_mem(st,OP_LD,dst,ix) < 0)
	    return -1;
	return dst;
    }
    return 0;
}


// generate LD/LI.. load value into a register if not already
NOINLINE int csp_load(csp_rt_t* st, rentry_t* rp)
{
    if (!rp->L && st->ap) { // not loaded and generate code
	int r;

	if (rp->X) {  // load variable
	    if ((r = map_reg(st, rp->ix)) < 0)
		return -1;
	}
	else if (rp->I) {
	    r = alloc_reg(st);
	    if (csp_load_value(st, r, rp->vt, rp->val) < 0)
		return -1;
	}
	rp->reg = r;
	rp->L = 1;
    }
    return rp->reg;
}


// Push immediate value (integer, float, or string constant)
NOINLINE static int push_imm(csp_rt_t* st, rentry_t* rstack, int ep,
		    vtype_t vt, value_t val)
{
    rstack[ep] = (rentry_t){ .reg = 0, .val = val, .vt  = vt,
			     .L=0, .I=1, .X=0 };
    return ep+1;
}

// Push string constant
NOINLINE static int push_str(csp_rt_t* st, rentry_t* rstack, int ep,
		    char* ptr, int len)
{
    index_t ix;

    ix = lookup_string_const(st, ptr, len);
    if (ix == BAD_INDEX) ix = new_string_const(st, ptr, len);
    if (ix == BAD_INDEX) return -1;
    rstack[ep] = (rentry_t){ .L=0, .X=1, .reg=0, .ix=ix, .vt=V_STRING };
    return ep+1;
}

// Push variable/declaration reference
NOINLINE static int push_var(csp_rt_t* st, rentry_t* rstack, int ep,
			     index_t ix, vtype_t vt)
{
    value_t val;
    int I = 0;

    if (st->decl[INDEX(ix)].type == DECL_CONSTANT) {
	I = 1;
	val = st->decl[INDEX(ix)].cn.init;
    }
    else if (st->decl[INDEX(ix)].type == DECL_VARIABLE) {
	add_var(st, ix);
	if (st->ev) {
	    I = 1;
	    val = csp_value(st, ix);
	}
    }
    else {
	val = rstack[ep].val;
    }
    rstack[ep] = (rentry_t) { .ix=ix, .val=val, .L=0, .I=I, .X=1, .vt = vt };
    return ep+1;
}

// Push L-value (assignment target, index only, no load)
NOINLINE static int push_lval(rentry_t* rstack, int ep, index_t ix, vtype_t vt)
{
    rstack[ep] = (rentry_t) { .ix=ix,.X=1,.L=0,.I=0,.vt=vt };
    return ep+1;
}

// Push register result (from operation)
NOINLINE static int push_reg(rentry_t* rstack, int ep, reg_t r, vtype_t vt,
			     value_t val, int imm)
{
    rstack[ep] = (rentry_t){.val=val,.reg=r,.X=0,.I=imm,.L=1,.vt=vt };
    return ep+1;
}

// Convert operand to float (int→float via cvtif)
NOINLINE static int coerce_to_float(csp_rt_t* st, rentry_t* e)
{
    rentry_t ent = *e;

    if (ent.vt == V_FLOAT) return 0;  // already float
    if (ent.vt != V_INTEGER) return -1;  // can only convert int

    // For variables (X=1), load first then convert
    if (ent.X && st->ap) {
	if (csp_load(st, &ent) < 0)
	    return -1;
    }

    if (ent.I && !ent.X) {  // pure immediate, not variable
	ent.val.f = op_CVTIF(ent.val.i);
	ent.I = 1;
	ent.L = 0;
    }
    else if (ent.L && st->ap) {
	reg_t r = alloc_reg(st);
	if (asm_CVTIF(st, r, ent.reg) < 0)
	    return -1;
	free_reg(st, ent.reg);
	ent.reg = r;
	ent.L = 1;
	ent.I = 0;
    }
    ent.vt = V_FLOAT;
    *e = ent;
    return 0;
}

// Convert operand to int (float→int via cvtfi)
NOINLINE static int coerce_to_int(csp_rt_t* st, rentry_t* e)
{
    rentry_t ent = *e;

    if (ent.vt == V_INTEGER) return 0;  // already int
    if (ent.vt != V_FLOAT) return -1;  // can only convert float

    // For variables (X=1), load first then convert
    if (ent.X && st->ap) {
	if (csp_load(st, &ent) < 0)
	    return -1;
    }

    if (ent.I && !ent.X) {  // pure immediate, not variable
	ent.val.i = op_CVTFI(ent.val.f);
	ent.I = 1;
	ent.L = 0;
    }
    else if (ent.L && st->ap) {
	reg_t r = alloc_reg(st);
	if (asm_CVTFI(st, r, ent.reg) < 0)
	    return -1;
	free_reg(st, ent.reg);
	ent.reg = r;
	ent.L = 1;
	ent.I = 0;
    }
    ent.vt = V_INTEGER;
    *e = ent;
    return 0;
}

// Process binary assignment operator: generates ST instruction
// Returns new ep on success, -1 on error
NOINLINE static int process_assign(csp_rt_t* st, opcode_t op, rentry_t* rstack, int ep)
{
    rentry_t lhs = rstack[ep-2];
    rentry_t rhs = rstack[ep-1];
    vtype_t ltype;

#ifdef DEBUG
    if (debug) {
    printf("ASSIGN ");
    print_rentry(st, "lhs", &lhs);
    print_rentry(st, "rhs", &rhs);
    printf("\n");
    }
#endif

    if (lhs.ix == BAD_INDEX) {
	// FIXME: error "left and side is not an lvalue"
	// can we print left hand side? maybe not worth it!
	csp_set_error(st, ERR_SYNTAX);
	return -1;
    }

    // Get target type from declaration
    ltype = st->decl[INDEX(lhs.ix)].vt;

    // Type conversion if needed
    if (ltype == V_INTEGER && (rhs.vt == V_FLOAT)) {
	if (coerce_to_int(st, &rhs) < 0)
	    return -1;
    } else if (ltype == V_FLOAT && (rhs.vt == V_INTEGER)) {
	if (coerce_to_float(st, &rhs) < 0)
	    return -1;
    }
    if (csp_load(st, &rhs) < 0)
	return -1;

    if (!rhs.L && st->ap) {
	// is this an internal error?
	csp_set_error(st, ERR_SYNTAX);  // rhs must have value
	return -1;
    }

    if (!st->ap) {
	if (rhs.I)
	    csp_set_value(st, lhs.ix, rhs.val);
    }
    else { // Generate store instruction
	if (asm_mem(st, op, rhs.reg, lhs.ix) < 0)
	    return -1;
    }
    // Result is the rhs (for chaining A=B=1)
    rstack[ep-2] = rhs;
    return ep - 1;
}

// Get float version of arithmetic opcode (or same if no float version)
NOINLINE static opcode_t float_op(opcode_t op)
{
    switch(op) {
    case OP_ADD: return OP_FADD;
    case OP_SUB: return OP_FSUB;
    case OP_MUL: return OP_FMUL;
    case OP_DIV: return OP_FDIV;
    case OP_NEG: return OP_FNEG;
    case OP_MOV: return OP_FMOV;
    case OP_LT:  return OP_FLT;
    case OP_LTE: return OP_FLTE;
    case OP_GT:  return OP_FGT;
    case OP_GTE: return OP_FGTE;
    case OP_EQEQ: return OP_FEQEQ;
    case OP_NEQ: return OP_FNEQ;
    default: return op;
    }
}

NOINLINE static int process_op(csp_rt_t* st, tok_t tok, rentry_t* rstack, int ep)
{
    int dst;
    opcode_t op;
    vtype_t rt;

    switch(op_table_arity(tok)) {
    case 2: {
	rentry_t* a = &rstack[ep-2];
	rentry_t* b = &rstack[ep-1];

	switch(tok) {
	case RIMP:
	    st->rimp = 0;
	    if ((ep = process_assign(st, OP_STIMP, rstack, ep)) < 0)
		return PARSE_ERROR;
	    break;
	case EQ:
	    if ((ep = process_assign(st, OP_ST, rstack, ep)) < 0)
		return PARSE_ERROR;
	    break;
	case COMMA:
	    // comma: side effects already done via ST, just keep right operand
	    if (a->L) free_reg(st, a->reg);
	    *a = *b;
	    ep--;
	    break;
	default: {
	    vtype_t at = a->vt;
	    vtype_t bt = b->vt;

	    // Type coerce: promote to float if either operand is float
	    if (at == V_FLOAT || bt == V_FLOAT) {
		if ((at == V_INTEGER) && (coerce_to_float(st, a) < 0))
		    return PARSE_ERROR;
		if ((bt == V_INTEGER) && (coerce_to_float(st, b) < 0))
		    return PARSE_ERROR;
		op = float_op(op_table_code(tok));
	    } else {
		op = op_table_code(tok);
	    }
	    rt = csp_opcode_rtype(op);
#ifdef DEBUG
	    if (debug) {
	    printf("op=%s\n", csp_opcode_name(op));
	    print_rentry(st, "L", a);
	    print_rentry(st, "R", b);
	    printf("\n");
	    }
#endif
	    if ((!st->ap || ( !a->X && !b->X ))
		&& a->I && b->I && (csp_opcode_arity(op) == 2)) {
		// constant fold
		value_t result = eval2(op, a->val, b->val);
		if (a->L) free_reg(st, a->reg);
		if (b->L && (a->reg != b->reg)) free_reg(st, b->reg);
		a->X = a->L = 0;
		a->I = 1;
		a->val = result;
	    }
	    else {
		if (csp_load(st, a) < 0) return -1;
		if (csp_load(st, b) < 0) return -1;
		if (a->L && b->L) {
		    dst = alloc_reg(st);
		    if (st->ap != NULL) {
			if (asm_bop(st, op, dst, a->reg, b->reg) < 0)
			    return PARSE_ERROR;
			free_reg(st, a->reg);
			if (a->reg != b->reg)
			    free_reg(st, b->reg);
		    }
		    a->reg = dst;
		    a->I = 0;
		    a->vt = rt;
		}
		else if (st->ap)
		    return -1;
		else {
		    a->I = a->L = a->X = 0;
		    a->vt = rt;
		}
	    }
	    ep--;
	}
	}
	break;
    }
    case 1: {
	rentry_t* a = &rstack[ep-1];
	vtype_t at = a->vt;

	// Select float op if operand is float
	if (at == V_FLOAT) {
	    op = float_op(op_table_code(tok));
	} else {
	    op = op_table_code(tok);
	}
	rt = csp_opcode_rtype(op);

#ifdef DEBUG
	if (debug) {
	printf("op=%s\n", csp_opcode_name(op));
	print_rentry(st, "A", a);
	printf("\n");
	}
#endif
	if (!a->X && a->I && (csp_opcode_arity(op) == 1)) { // constant fold
	    value_t result = eval1(op, a->val);
	    if (a->L) free_reg(st, a->reg);
	    a->val = result;
	    a->X = a->L = 0;
	    a->I = 1;
	}
	else {
	    if (csp_load(st, a) < 0) return -1;
	    if (a->L) { // generate code
		dst = alloc_reg(st);
		if (st->ap != NULL) {
		    if (asm_uop(st, op, dst, a->reg) < 0)
			return PARSE_ERROR;
		    free_reg(st, a->reg);
		}
		a->reg = dst;
		a->I = 0;
		a->vt = rt;
	    }
	    else if (st->ap)
		return -1;
	    else
		a->I = a->L = a->X = 0;
	}
	a->vt = rt;
	break;
    }
    case 0:
	return PARSE_ERROR;
    }
    return ep;
}

//       rstack
//  0    arg0   ep-(4-0)
//  1    arg1   ep-(4-1)
//  2    arg2   ep-(4-2)
//  3    arg3   ep-(4-3)
//  ep
//
NOINLINE static int process_fcall(csp_rt_t* st, token_t* word, uint8_t arity,
				  rentry_t* rstack, int ep)
{
    int dst, n, j;
    const csp_func_t* func = NULL;
    uint16_t argcode = 0;
    uint8_t argimm = 0;
    // int func_res;
    int is_user;
    int func_idx;
    rentry_t* rarg = &rstack[ep-arity]; // first arg
    value_t dval;
    int imm = 0;

    if ((func = csp_match_func(st, &word->v.str, arity,
			       rarg, &is_user, &func_idx)) == NULL)
	return -1;
    // FIXME: handle, changed(x), timeout(t) whith ops
    n = arity;
    for (j = 0; j < n; j++) {
	rentry_t arg = rarg[j];
	vtype_t argvt = arg.vt;
	vtype_t argtype = fn_type(func, j); // read RO data!

	argcode |= (argvt << 4*j);
	argimm  |= (arg.I << j);

	// check arguments & coerce
	switch(argtype) {
	case V_ANY:
	    break;  // OK
	case V_NUMBER:
	    if (!((argvt == V_INTEGER) || (argvt == V_FLOAT)))
		goto type_mismatch;
	    break;
	case V_INTEGER:
	    if (coerce_to_int(st, &arg) < 0)
		goto type_mismatch;
	    break;
	case V_FLOAT:
	    if (coerce_to_float(st, &arg) < 0)
		goto type_mismatch;
	    break;
	case V_STRING:
	    if (argvt != V_STRING)
		goto type_mismatch;
	    break;
	case V_TIMER:
	case V_DIGITAL:
	case V_ANALOG:
	case V_CAN:
	case V_INDEX:
	    // check object type !!!
	    if (!arg.X) { // must be a "variable"
		goto type_mismatch;
	    }
	    arg.X = 0;
	    arg.I = 1;
	    arg.val.u = arg.ix;
	    break;
	default:
	    if (argtype != argvt)
		goto type_mismatch;
	    break;
	}
	if (csp_load(st, &arg) < 0) {
	    csp_set_error(st, ERR_INTERNAL_ERROR);
	    return -1;
	}
	if (asm_ARG(st, arg.reg, j) < 0) {
	    csp_set_error(st, ERR_INTERNAL_ERROR);
	    return -1;
	}
	if (arg.L) free_reg(st, arg.reg);
    }
    // check if we can evaluate a pure function
    // note that we may have generated arguments anyway, may be
    // optimise to remove extra instructions in the future
    // we may have to do a "dryrun" to check if function is pure
    // or we have special functions
    imm = (argimm == ((1 << arity)-1));
    if (func_pure(func,0) && imm) {
	value_t arg[MAX_ARGS];
	csp_func_fn fn = NULL;

	if (is_user)
	    fn = func_fn(st->ufuncs, func_idx);
	else
	    fn = func_fn(csp_builtin_funcs, func_idx);
	for (j = 0; j < arity; j++)
	    arg[j] = rarg[j].val;
	dval = fn(st, argcode, arg, arity);
    }

    // pop rstack
    if (n > 0) {
	ep -= n;
    }
    dst = alloc_reg(st);
    if (asm_call(st, dst, func_idx, is_user, argcode) < 0)
	return 0;
    return push_reg(rstack, ep, dst, func->rtype, dval, imm);

type_mismatch:
    if (csp_set_error(st, ERR_FUNCTION_ARGUMENT_TYPE_MISMATCH)) {
	csp_set_err_arg_tstr(st, 0, &word->v.str);
	csp_set_err_arg_int(st, 1, arity);
	csp_set_err_arg_int(st, 2, j);
    }
    return -1;
}

void print_stack_used()
{
    // stack debug
    csp_print_str("StackUsed=");
    csp_print_int(stack_used());
    csp_println();
}

// num_toks is number of tokens on input, consumed on output
// result receives the expression result (reg, immediate flag, value, type)
// returns: 1=ok, 0=error
NOINLINE int csp_parse_expr(csp_rt_t* st, token_t* tv, size_t* num_toks,
		   rentry_t* result)
{
    tok_t tok;
    tokval_t tval;
    tok_t ptok = NONE;   // previous operator/token
    int pp = 0;         // operator stack pointer
    int ep = 0;         // expression stack pointer
    uint32_t ostack[MAX_PARSE_STACK_DEPTH];  // stack of operators
    rentry_t rstack[MAX_PARSE_STACK_DEPTH];  // stack of {reg, index}
    index_t ix;
    int i = 0;
    size_t n = *num_toks;
    int in_func = 0;

next:
    if ((i >= n) || (tv[i].t==NEWLINE) || (tv[i].t==NONE))  // end-of-list
	goto out;
    tok  = tv[i].t;
    tval = tv[i].v;
    i++;
    switch(tok) {
    case QUEST: i--; goto out;
    case PLUS:
	if ((ptok == NONE) || (ptok == RP) ||
	    ((ptok != INT) && (ptok != WORD) && (ptok != FLT)))
	    tok = PLUS1;
	goto operator;
    case MINUS:
	if ((ptok == NONE) || (ptok == RP) ||
	    ((ptok != INT) && (ptok != WORD) && (ptok != FLT)))
	    tok = MINUS1;
	goto operator;
    case LP:
	ostack[pp++] = LP; ptok = LP; break;
    case RP:
	if (pp == 0)
	    return 0;
	// Process operators until we hit LP or a function marker
	// Check if we're inside a function call

	for (int k = pp-1; k >= 0; k--) {
	    if (IS_FUNC_MARKER(ostack[k])) { in_func = 1; break; }
	    if (ostack[k] == LP) break;
	}
	while(pp && ((tok = ostack[pp-1]) != LP) && !IS_FUNC_MARKER(tok)) {
	    // COMMA inside function call: just pop it, don't combine args
	    if (tok == COMMA && in_func) {
		pp--;
		continue;
	    }
	    if ((ep = process_op(st, tok, rstack, ep)) < 0)
		return 0;
	    pp--;
	}
	if (pp && IS_FUNC_MARKER(ostack[pp-1])) {
	    uint32_t marker = ostack[--pp];
	    int j = FUNC_MARKER_TIX(marker);
	    int ep0 = FUNC_MARKER_EP(marker);
	    uint8_t arity = ep - ep0;

	    if ((ep = process_fcall(st, &tv[j], arity, rstack, ep)) < 0) {
		csp_set_error(st, ERR_SYNTAX);
		return 0;
	    }
	}
	else if (pp && (ostack[pp-1] == LP)) {
	    pp--;  // pop the LP for regular parentheses
	}
	else {
	    return 0;  // mismatched )
	}
	tok = RP;
	ptok = INT;
	break;
    case INT:
	if ((ep = push_imm(st, rstack, ep, V_INTEGER, tval.val)) < 0)
	    return 0;
	ptok = INT;
	break;
    case FLT:
	if ((ep = push_imm(st, rstack, ep, V_FLOAT, tval.val)) < 0)
	    return 0;
	ptok = FLT;
	break;
    case STR:
	if ((ep = push_str(st, rstack, ep, tval.str.ptr, tval.str.len)) < 0)
	    return 0;
	ptok = STR;
	break;
    case WORD: {
	if (tv[i].t == LP) {
	    // It's a function call - push marker to ostack and skip LP
	    ostack[pp] = MAKE_FUNC_MARKER(i-1, ep);
	    pp++;
	    i++;  // skip the LP token
	    ptok = LP;
	}
	else {
	    vtype_t vt;
	    // Not a function - regular variable/decl/state lookup
	    if ((ix = lookup_decl(st,&tval.str)) == BAD_INDEX) {
		if (csp_set_error(st, ERR_VARIABLE_NOT_DECLARED)) {
		    csp_set_err_arg_tstr(st, 0, &tval.str);
		}
		return 0;
	    }
	    // Handle obj.field access
	    if ((st->decl[INDEX(ix)].type == DECL_OBJECT) &&
		(tv[i].t == DOT) && (tv[i+1].t == WORD)) {
		index_t mx = st->decl[INDEX(ix)].mq.mx;  // module def
		ivalue_t dn = st->decl[INDEX(mx)].md.n;  // number of elements
		index_t jx;
		tval = tv[i+1].v;
		if ((jx = lookup_decl_in(st, &tval.str,
					 INDEX(mx)+1,INDEX(mx)+1+dn)) == BAD_INDEX) {
		    if (csp_set_error(st, ERR_FIELD_NOT_FOUND)) {
			csp_set_err_arg_tstr(st, 0, &tval.str);
		    }
		    return 0;
		}
		ix = MAKE_INDEX(st->decl[INDEX(ix)].mq.m,INDEX(jx));
		i += 2;
	    }
	    // Apply module context
	    if ((st->mdef != BAD_INDEX) && (OBJ(ix) == 0))
		ix = MAKE_INDEX(CURRENT, INDEX(ix));

	    // Check if this is an l-value (assignment target)
	    vt = st->decl[INDEX(ix)].vt;
	    if ((i < n) && ((tv[i].t == EQ)||(tv[i].t == RIMP))) {
		// L-value: push index only, no load
		ep = push_lval(rstack, ep, ix, vt);
	    }
	    else {
		if ((ep = push_var(st, rstack, ep, ix, vt)) < 0)
		    return 0;
	    }
	    ptok = WORD;
	}
	break;
    }
    default:
	if (op_table_arity(tok) > 0)
	    goto operator;
	i--;      // return failed token
	goto out; // let tok terminate exprssion
	// return 0;
    }
    goto next;
operator:
    {
	int p1;
	if ((p1 = op_table_prec(tok)) == -1)
	    return 0;
	if (pp == 0) {
	    if (tok == RIMP) { st->rimp = 1; }
	    ostack[pp++] = tok;
	}
	else {
	    tok_t tok2 = ostack[pp-1];
	    int p2;
	    // FUNC_MARKER acts like LP - don't process operators past it
	    if (IS_FUNC_MARKER(tok2) || tok2 == LP) {
		if (tok == RIMP) { st->rimp = 1;  }
		ostack[pp++] = tok;
		ptok = tok;
		goto next;
	    }
	    p2 = op_table_prec(tok2);

	    while ( ((p2 > p1) && (tok2 != LP)) ||
		    ((p2 == p1) && (op_table_assoc(tok2) < 0))) {
		if ((ep = process_op(st, tok2, rstack, ep)) < 0)
		    return 0;
		pp--;
		if (pp == 0) break;
		tok2 = ostack[pp-1];
		if (IS_FUNC_MARKER(tok2) || (tok2 == LP)) break;
		p2 = op_table_prec(tok2);
	    }
	    if (tok == RIMP) { st->rimp = 1; }
	    ostack[pp++] = tok;
	}
	ptok = tok;
    }
    goto next;
out: // expression is terminated with non-expression char
    while(pp > 0) {
	tok = ostack[--pp];
	if (tok == LP)
	    return 0;
	if ((ep = process_op(st, tok, rstack, ep)) < 0)
	    return 0;
    }
    if (pp < 0)
	return 0;
    if (ep == 1) {
	*num_toks = i;
	if (result)
	    *result = rstack[0];
	return 1;
    }
    return 0;
}

// parse expr while turn of codegen is the same as partial eval
NOINLINE int csp_parse_const_expr(csp_rt_t* st, token_t* tv, size_t* num_toks,
				  rentry_t* result)
{
    reg_allocator_t* saved_ap = st->ap;
    int r;
    st->ap = NULL;  // no codegen
    r = csp_parse_expr(st, tv, num_toks, result);
    st->ap = saved_ap;
    return r;
}

typedef struct {
    tstr_t name;
} module_param_t;

// '#' 'module' <name>
static const uint8_t module_pat[] = {
    P_TOK, HASH,
    P_TOK, MODULE,
    P_STR, csp_offsetof(module_param_t, name),
    P_END
};

NOINLINE int csp_parse_module(csp_rt_t* st, token_t* tv, size_t n)
{
    module_param_t d;
    index_t ix, jx;
    int i;

    if (pmatch(st, tv, n, module_pat, &d) < 0) {
	csp_set_error(st, ERR_SYNTAX);
	return -1;
    }
    if ((ix = csp_new_udecl(st, &d.name, DECL_MODULE)) == BAD_INDEX)
	return -1;
#if defined(SUPPORT_STATES) && (SUPPORT_STATES==1)
    {
	// create a local state variable (if states are supported)
	// maybe only if #states are defined in module context?
	const tstr_t State = { .ptr = "State", .len = 5};
	index_t ix;
	st->save_sx = st->sx;
	ix = csp_new_decl(st, &State, DECL_VARIABLE);
	st->sx = MAKE_INDEX(CURRENT, INDEX(ix));
    }
#endif
    st->mdef = ix;  // current module being defined
    if ((jx = asm_enter(st, 0, ix)) < 0)
	return -1;
    st->ent = jx;   // entry point of module being defined
    i = INDEX(ix);
    st->decl[i].md.n = 0;
    st->decl[i].md.ent = st->ent;
    return 0;
}

typedef struct {
} end_param_t;

// '#' 'end' [....]
static const uint8_t end_pat[] = {
    P_TOK, HASH,
    P_TOK, END,
    P_END
};

NOINLINE int csp_parse_end(csp_rt_t* st, token_t* tv, size_t n)
{
    end_param_t d;
    index_t mx, ex, lx;
    const tstr_t empty = { .ptr = NULL, .len = 0};
    
    if (pmatch(st, tv, n, end_pat, &d) < 0) {
	csp_set_error(st, ERR_SYNTAX);
	return -1;
    }

#if defined(SUPPORT_STATES) && (SUPPORT_STATES==1)
    if (st->sdef >= 0) {
	st->sdef = -1;
	return 0;
    }
#endif
    if ((mx = st->mdef) == BAD_INDEX) {
	csp_set_error(st, ERR_END_MISMATCH);
	return -1;  // no module
    }
    if ((ex = csp_new_decl(st, &empty, DECL_END)) == BAD_INDEX)
	return -1;
    st->decl[INDEX(mx)].md.n = (INDEX(ex) - INDEX(mx)) - 1;
    if ((lx = asm_leave(st, 0, 0)) < 0)
	return -1;
    // ent MUST be OP_ENTER!
    st->instr[st->ent].e.num = (lx - st->ent - 1);
    st->instr[lx].v.num = st->instr[st->ent].e.num;
    st->instr[lx].v.mx  = st->instr[st->ent].e.mx;
    // stack?
    st->mdef = BAD_INDEX;
    st->ent = 0;
#if defined(SUPPORT_STATES) && (SUPPORT_STATES==1)
    st->sx   = st->save_sx;
#endif
    return 0;
}

// '#' 'variable' <name>[':' <size>] [<opt>+] ['=' <num>]
// <opt> := 'in'|'out'|'inout'|  -- when use as argument in module
//          'signed'|'unsigned'|'float'
// Note that in is used for input arguments in objects
// and out is used for output argumets

typedef struct {
    tstr_t name;
    ivalue_t res;
    decl_opts_t opts;
    value_t init;
} variable_param_t;

static const uint8_t variable_pat[] = {
    P_TOK, HASH,
    P_TOK, VARIABLE,
    P_STR, csp_offsetof(variable_param_t, name),
    P_OPT,5,P_TOK,COLON,P_INTEGER,csp_offsetof(variable_param_t,res),P_END,
    P_OPTS,csp_offsetof(variable_param_t, opts),
    P_OPT, 6, P_TOK, EQ, P_NUMBER,
    csp_offsetof(variable_param_t, opts), // pick up vt here
    csp_offsetof(variable_param_t, init), P_END,
    P_END
};

//
NOINLINE int csp_parse_variable(csp_rt_t* st, token_t* tv, size_t n)
{
    variable_param_t d = {0};
    index_t ix;
    int i;

    // set default values
    d.res = 8*sizeof(ivalue_t);
    d.opts.vt = V_INTEGER;

    if (pmatch(st, tv, n, variable_pat, &d) < 0) {
	csp_set_error(st, ERR_SYNTAX);
	return -1;
    }
    if ((ix = csp_new_udecl(st, &d.name, DECL_VARIABLE)) == BAD_INDEX)
	return -1;
    i = INDEX(ix);
    st->decl[i].vt = d.opts.vt;
    st->decl[i].res = MAKE_RES(d.res);
    st->decl[i].dir = d.opts.dir;
    st->decl[i].va.init = d.init;
    return 0;
}

// '#' 'constant' <name>[':' <size>] [<opt>+] '=' <num>
typedef struct {
    tstr_t name;
    ivalue_t res;
    decl_opts_t opts;
    value_t init;
} constant_param_t;

static const uint8_t constant_pat[] = {
    P_TOK, HASH,
    P_TOK, CONSTANT,
    P_STR, csp_offsetof(constant_param_t, name),
    P_OPT,5,P_TOK,COLON,P_INTEGER,csp_offsetof(constant_param_t,res),P_END,
    P_OPTS,csp_offsetof(constant_param_t, opts),
    P_TOK, EQ, P_NUMBER,
    csp_offsetof(constant_param_t, opts), // pick up vt here
    csp_offsetof(constant_param_t, init),
    P_END
};

NOINLINE int csp_parse_constant(csp_rt_t* st, token_t* tv, size_t n)
{
    constant_param_t d = {0};
    index_t ix;
    int i;

    // set default values
    d.res = 8*sizeof(ivalue_t);
    d.opts.vt = V_INTEGER;

    if (pmatch(st, tv, n, constant_pat, &d) < 0) {
	csp_set_error(st, ERR_SYNTAX);
	return -1;
    }
    if ((ix = csp_new_udecl(st, &d.name, DECL_CONSTANT)) == BAD_INDEX)
	return -1;
    i = INDEX(ix);
    st->decl[i].vt = d.opts.vt;
    st->decl[i].res = MAKE_RES(d.res);
    st->decl[i].cn.init = d.init;
    return 0;
}

// '#' 'digital' <name> [<iodir>|<pull>] [<port>':']<pin>
typedef struct {
    tstr_t name;
    ivalue_t port;
    ivalue_t pin;
    decl_opts_t opts;
} digital_param_t;

static const uint8_t digital_pat[] = {
    P_TOK, HASH,
    P_TOK, DIGITAL,
    P_STR, csp_offsetof(digital_param_t, name),
    P_OPTS, csp_offsetof(digital_param_t, opts),
    P_ALT, 2,
	// Alt 1: port:pin (7 bytes: P_INTEGER,off,P_TOK,COLON,P_INT,off,P_END)
    7, P_INTEGER, csp_offsetof(digital_param_t, port),
    P_TOK, COLON,
    P_INTEGER, csp_offsetof(digital_param_t, pin),
    P_END,
    // Alt 2: just pin (3 bytes: P_INT,off,P_END)
    3, P_INTEGER, csp_offsetof(digital_param_t, pin),
    P_END,
    P_END
};

NOINLINE int csp_parse_digital(csp_rt_t* st, token_t* tv, size_t n)
{
    digital_param_t d = {0};
    index_t ix;
    int i;

    if (pmatch(st, tv, n, digital_pat, &d) < 0) {
	csp_set_error(st, ERR_SYNTAX);
	return -1;
    }
    if (d.opts.dir == 0) d.opts.dir = DIR_IN;

    if ((ix = csp_new_udecl(st, &d.name, DECL_DIGITAL)) == BAD_INDEX)
	return -1;    
    i = INDEX(ix);
    st->decl[i].res = MAKE_RES(1);
    st->decl[i].di.pin = d.pin;
    st->decl[i].di.port = d.port;
    st->decl[i].dir = d.opts.dir;
    st->decl[i].di.pullup = d.opts.pullup;
    st->decl[i].di.pulldown = d.opts.pulldown;
    return 0;
}

typedef struct {
    tstr_t name;
    ivalue_t res;
    ivalue_t port;
    ivalue_t pin;
    decl_opts_t opts;
} analog_param_t;

static const uint8_t analog_pat[] = {
    P_TOK, HASH,
    P_TOK, ANALOG,
    P_STR, csp_offsetof(analog_param_t, name),
    P_OPT, 5,P_TOK,COLON,P_INTEGER, csp_offsetof(analog_param_t, res), P_END,
    P_OPTS, csp_offsetof(analog_param_t, opts),
    P_ALT, 2,
	// Alt 1: port:pin (7 bytes: P_INT,off,P_TOK,COLON,P_INT,off,P_END)
	7, P_INTEGER, csp_offsetof(analog_param_t, port),
	   P_TOK, COLON,
	   P_INTEGER, csp_offsetof(analog_param_t, pin),
	   P_END,
	// Alt 2: just pin (3 bytes: P_INTEGER,off,P_END)
	3, P_INTEGER, csp_offsetof(analog_param_t, pin),
	   P_END,
    P_END
};

//'#' 'analog' <name> [':'<size>] [<opt>*]  [<port>':'] <pin>
//   <opt> := 'in' | 'out' | 'inout' | 'pwm' | 'float' | 'signed' | 'unsigned'
NOINLINE int csp_parse_analog(csp_rt_t* st, token_t* tv, size_t n)
{
    analog_param_t d = {0};
    index_t ix;
    int i;

    d.res = 10;
    d.opts.vt = V_INTEGER;
    if (pmatch(st, tv, n, analog_pat, &d) < 0) {
	csp_set_error(st, ERR_SYNTAX);
	return -1;
    }
    if (d.opts.dir == 0) d.opts.dir = DIR_IN;

    if ((ix = csp_new_udecl(st, &d.name, DECL_ANALOG)) == BAD_INDEX)
	return -1;
    i = INDEX(ix);
    st->decl[i].vt = d.opts.vt;
    st->decl[i].res = MAKE_RES(d.res);
    st->decl[i].an.pin = d.pin;
    st->decl[i].an.port = d.port;
    st->decl[i].dir = d.opts.dir;
    st->decl[i].an.pwm = d.opts.pwm;
    st->decl[i].an.endian = d.opts.endian;    
    return 0;
}

typedef struct {
    tstr_t name;
    ivalue_t timeout;
    ivalue_t init;
} timer_param_t;

static const uint8_t timer_pat[] = {
    P_TOK, HASH,
    P_TOK, TIMER,
    P_STR, csp_offsetof(timer_param_t, name),
    P_INTEGER, csp_offsetof(timer_param_t, timeout),
    P_OPT, 5, P_TOK, EQ, P_INTEGER, csp_offsetof(timer_param_t, init), P_END,
    P_END
};

NOINLINE int csp_parse_timer(csp_rt_t* st, token_t* tv, size_t n)
{
    timer_param_t d = {0};
    index_t tm, tx;
    int i;
    const tstr_t empty = { .ptr = NULL, .len = 0};

    d.init = 0;
    if (pmatch(st, tv, n, timer_pat, &d) < 0) {
	csp_set_error(st, ERR_SYNTAX);
	return -1;
    }
    if ((tm = csp_new_udecl(st, &d.name, DECL_TIMER)) == BAD_INDEX)
	return -1;
    tx = csp_new_decl(st, &empty, DECL_VARIABLE);
    if (tx != tm+1) {
	csp_set_error(st, ERR_INTERNAL_ERROR);
	return -1;
    }
    i = INDEX(tx);
    st->decl[i].vt = V_UNSIGNED;
    st->decl[i].res = MAKE_RES(32);
    st->decl[i].va.init.u = 0;

    i = INDEX(tm);
    st->decl[i].vt = V_TIMER;
    st->decl[i].tm.fired = 0;
    st->decl[i].tm.init = d.init;
    st->decl[i].tm.period = d.timeout;
    return 0;
}

// '#' 'can' <name>[':'<size>] [<opt>*] <can-bit>
// <opt> := 'in' | 'out' | 'inout' | 'float' | 'signed' | 'unsigned'
//
// <can-bit> :=
//  <frame-id> '[' <bit-pos> ']'
//  <frame-id> '[' <bit-pos> '..' <bit-pos> ']'
//
typedef struct {
    tstr_t name;
    ivalue_t res;
    ivalue_t frameid;
    ivalue_t bit0;
    ivalue_t bit1;
    decl_opts_t opts;
} can_param_t;

static const uint8_t can_pat[] = {
    P_TOK, HASH,
    P_TOK, CAN,
    P_STR, csp_offsetof(timer_param_t, name),
    P_OPT, 5, P_TOK, COLON, P_INTEGER, csp_offsetof(can_param_t, res), P_END,
    P_OPTS, csp_offsetof(can_param_t, opts),
    P_INTEGER, csp_offsetof(can_param_t, frameid),
    P_TOK, LB,
    P_ALT, 2,
    // note the longer pattern first !!!
    9, P_INTEGER, csp_offsetof(can_param_t, bit0), P_TOK, DOT, P_TOK, DOT,
       P_INTEGER, csp_offsetof(can_param_t, bit1), P_END,
    3, P_INTEGER, csp_offsetof(can_param_t, bit0), P_END,
    P_TOK, RB,
    P_END
};

NOINLINE int csp_parse_can(csp_rt_t* st, token_t* tv, size_t n)
{
    can_param_t d = {0};
    value_t vid;
    index_t ix, idx;
    int i, len;

    d.res = d.bit0 = d.bit1 = d.frameid = -1;
    if (pmatch(st, tv, n, can_pat, &d) < 0) {
	csp_set_error(st, ERR_SYNTAX);
	return -1;
    }
    if ((ix = csp_new_udecl(st, &d.name, DECL_CAN)) == BAD_INDEX)
	return -1;
    vid.i = d.frameid;
    idx = lookup_const(st, V_INTEGER, vid);
    if (idx == BAD_INDEX)
	idx = new_signed_const(st, vid.i);
    if ((d.bit0 >= 0) && (d.bit1 >= d.bit0))
	len = (d.bit1 - d.bit0)+1;
    else if ((d.res > 0) && (d.bit0 >= 0))
	len = d.res;
    else {
	csp_set_error(st, ERR_SYNTAX);
	return -1;
    }

    i = INDEX(ix);
    st->decl[i].res = MAKE_RES(d.res);
    st->decl[i].vt = d.opts.vt;
    st->decl[i].dir = d.opts.dir;
    st->decl[i].ca.id = idx;
    st->decl[i].ca.bit = d.bit0;
    st->decl[i].ca.len = MAKE_CAN_LEN(len);
    st->decl[i].ca.endian = d.opts.endian;
    return 0;
}

#define MAX_INITS 8

typedef struct {
    tstr_t field;       // field name
    token_t* expr_tv;   // pointer to expression tokens
    int expr_len;       // number of tokens
    uint8_t reactive;   // 1 if <-, 0 if =
} init_entry_t;

typedef struct {
    tstr_t mod_name;
    tstr_t obj_name;
    init_entry_t inits[MAX_INITS];
    uint8_t ninits;
} object_param_t;

#define CB_STATIC_INIT   0
#define CB_REACTIVE_INIT 1

// Callback: save init entry - returns tokens consumed
static int cb_init_entry(csp_rt_t* st, token_t* tv, int ti, void* data, int reactive)
{
    object_param_t* d = (object_param_t*)data;
    init_entry_t* e;
    int k;
    
    if (d->ninits >= MAX_INITS)
	return 0;
    e = &d->inits[d->ninits];
    // e->field already set by P_STR with correct array indexing
    e->reactive = reactive;
    e->expr_tv = &tv[ti];  // save pointer to expression start
    // Find end of expression when end of line or next var assign is found
    // tokens <word> '='
    // tokens <word> '<-'
    // tokens \n
    k = ti;
    while (k < MAX_LINE_TOKENS && tv[k].t != NEWLINE) {
	if ((tv[k].t == WORD) && (k+1 < MAX_LINE_TOKENS) &&
	    ((tv[k+1].t == EQ) || (tv[k+1].t == RIMP)))
	    break;
	k++;
    }
    e->expr_len = k - ti;
    if (e->expr_len == 0) { csp_set_error(st, ERR_SYNTAX); return 0; }
    d->ninits++;
    return e->expr_len;  // return tokens consumed
}

static int cb_static_init(csp_rt_t* st, token_t* tv, int ti, void* data)
{
    return cb_init_entry(st, tv, ti, data, 0);
}

static int cb_reactive_init(csp_rt_t* st, token_t* tv, int ti, void* data)
{
    return cb_init_entry(st, tv, ti, data, 1);
}

// Generate code for static init: target = expr
// FIXME: how to write to target.d.pin / target.a.pin?????
// FIXME: generate cycle() == 0 condition ?
static int gen_static_init(csp_rt_t* st, init_entry_t* e,
			   index_t target, csp_part_t part)
{
    size_t num = e->expr_len;
    rentry_t rval;
    if (!csp_parse_expr(st, e->expr_tv, &num, &rval))
	return 0;
    if (!rval.L) csp_load(st, &rval);
    if (asm_mem_part(st, OP_ST, rval.reg, target, part) < 0)
	return 0;
    free_reg(st, rval.reg);
    return 1;
}

// Generate code for reactive init: target <- expr (with CHG/RULE)
static int gen_reactive_init(csp_rt_t* st, init_entry_t* e,
			     index_t target, csp_part_t part)
{
    size_t num = e->expr_len;
    rentry_t rval;
    int j;
    
    // Dry-run: collect variables
    st->nvar = 0;
    st->rimp = 1;
    csp_parse_const_expr(st, e->expr_tv, &num, &rval);
    st->rimp = 0;
    num = e->expr_len;

    // Generate CHG/RULE
    int cnd = alloc_reg(st);
    if (st->nvar > 0) {
	int k;
	if (asm_LI(st, cnd, 0) < 0)
	    return 0;
	for (k = 0; k < st->nvar; k++)
	    if (asm_mem(st, OP_CHG, cnd, st->var[k]) < 0)
		return 0;
    }
    else {
	if (asm_LI(st, cnd, -1) < 0)
	    return 0;
    }
    if ((j = csp_new_rule(st, cnd, 0)) < 0)
	return 0;
    free_reg(st, cnd);
    
    // Parse expression for real
    st->rimp = 1;
    if (!csp_parse_expr(st, e->expr_tv, &num, &rval)) {
	st->rimp = 0;
	return 0;
    }
    st->rimp = 0;

    if (!rval.L) csp_load(st, &rval);
    if (asm_mem_part(st, OP_STIMP, rval.reg, target, part) < 0)
	return 0;
    st->instr[j].r.nxt = st->ps.nn - j;
    if (csp_new_next(st, rval.reg) < 0)
	return 0;
    free_reg(st, rval.reg);
    return 1;
}

// '#' ModName ObjName (Field (=|<-) Expr)*
static const uint8_t object_pat[] = {
    P_TOK, HASH,
    P_STR, csp_offsetof(object_param_t, mod_name),
    P_STR, csp_offsetof(object_param_t, obj_name),
    P_REP, 19,
	P_ARRAY, csp_offsetof(object_param_t, inits), sizeof(init_entry_t),
	P_STR, csp_offsetof(init_entry_t, field),
	P_ALT, 2,
	    5, P_TOK, EQ, P_CALL, CB_STATIC_INIT, P_END,
	    5, P_TOK, RIMP, P_CALL, CB_REACTIVE_INIT, P_END,
    P_END
};

NOINLINE int csp_parse_object(csp_rt_t* st, token_t* tv, size_t n)
{
    object_param_t d = {0};
    index_t mx, ix;
    int i, m, k;
    ivalue_t mod_n;

    // Register callbacks
    pmatch_set_cb(CB_STATIC_INIT, cb_static_init);
    pmatch_set_cb(CB_REACTIVE_INIT, cb_reactive_init);

    // Parse: # ModName ObjName (Field (=|<-) Expr)*
    if (pmatch(st, tv, n, object_pat, &d) < 0) {
	if (st->ps.err == ERR_OK)
	    csp_set_error(st, ERR_SYNTAX);
	return -1;
    }

    // Lookup module
    if ((mx = lookup_decl(st, &d.mod_name)) == BAD_INDEX) {
	if (csp_set_error(st, ERR_MODULE_NOT_DECLARED)) {
	    csp_set_err_arg_tstr(st, 0, &d.mod_name);
	}
	return -1;
    }
    if (st->decl[INDEX(mx)].type != DECL_MODULE) {
	if (csp_set_error(st, ERR_NOT_A_MODULE)) {
	    csp_set_err_arg_tstr(st, 0, &d.mod_name);
	}
	return -1;
    }
    if (st->ps.nq >= MAX_OBJECTS-1) {
	csp_set_error(st, ERR_TOO_MANY_OBJECTS);
	return -1;
    }
    if ((ix = csp_new_udecl(st, &d.obj_name, DECL_OBJECT)) == BAD_INDEX)
	return -1;

    // Set up object slot
    i = INDEX(ix);
    st->decl[i].mq.mx = mx;
    m = st->ps.nq + 1;
    st->decl[i].mq.m = m;
    st->object[m] = ix;
    st->ps.nq++;

    // Generate code for init list
    mod_n = st->decl[INDEX(mx)].md.n;
    for (k = 0; k < d.ninits; k++) {
	int fi;
	index_t target;
	csp_part_t part = PART_VAL;
	init_entry_t* e = &d.inits[k];
	index_t fx = lookup_decl_in(st, &e->field,
				    INDEX(mx)+1, INDEX(mx)+1+mod_n);
	if (fx == BAD_INDEX) {
	    if (csp_set_error(st, ERR_FIELD_NOT_FOUND)) {
		csp_set_err_arg_tstr(st, 0, &e->field);
	    }
	    return -1;
	}
	// For timers, write to px (timeout constant), not timer decl
	fi = INDEX(fx);
	switch(st->decl[fi].type) {
	case DECL_TIMER:
	    part = PART_PERIOD;
	    break;
	case DECL_DIGITAL: // write to pin (value);
	    break;
	default:
	    break;
	}
	target = MAKE_INDEX(m, fi);
	if (e->reactive) {
	    if (!gen_reactive_init(st, e, target, part)) return -1;
	} else {
	    if (!gen_static_init(st, e, target, part)) return -1;
	}
    }

    // Generate NEW after init list (module rules run with correct values)
    if (asm_new(st, st->decl[INDEX(mx)].md.ent, ix) == BAD_INDEX)
	return -1;

    return 0;
}

// <expr> '?' <cond>
// expr = tok[0]...tok[i-1]
// cond = tok[i+1]...tok[num-1]
// first parse condition
// then parse expression
//
// <rule> ==
//        <expr1> '<-' <expr2> [ '?' <cond> ]
//
//     ==> <expr1> = <expr2> ? changed(vars-in-expr2) || <cond>
//
// <rule> ==
//         <expr1> ? <cond>
//
// generates code like
// BEGIN:
//    generate ( <cond> )
//    RULE: if !<cond> goto END
//    generate ( <expr> )
//  END:
//    next

//
// <rule> == <expr> [ '?' <expr> ]
//

// Example: mixed assignments and extra condition
//
//  A = 1, B <- C+D ? C > 5
//
// CONDITION =
//    C>5 && changed(C)||changed(D)
// BODY =
//    A = 1
//    B = C+D
//

typedef struct {
    pexpr_t body;
    pexpr_t cond;
} rule_param_t;

static const uint8_t rule_pat[] = {
    P_EXPR, csp_offsetof(rule_param_t, body),
    P_OPT, 5, P_TOK, QUEST,
	   P_EXPR, csp_offsetof(rule_param_t, cond),
	   P_END,
    P_END
};

NOINLINE int csp_parse_rule(csp_rt_t* st, token_t* tv, size_t n)
{
    size_t num;
    rule_param_t d;
    rentry_t rbody;
    int j;
    int cnd = -1;
    int cnd2 = -1;
    // int ncnd = 0;

    d.cond.len = 0;
    d.cond.pos = 0;
    if (pmatch(st, tv, n, rule_pat, &d) < 0) {
	csp_set_error(st, ERR_SYNTAX);
	return -1;
    }
#if defined(SUPPORT_STATES) && (SUPPORT_STATES==1)
    if (st->sdef >= 0) {  // we are in a state!
	int sr;
	cnd = alloc_reg(st);
	if (asm_mem(st,OP_LD,cnd,st->sx) < 0) // load state into cnd
	    return -1;
	sr = alloc_reg(st);
	if (asm_LI(st, sr, st->sdef) < 0)
	    return -1;
	if (asm_EQEQ(st, cnd, cnd, sr) < 0)
	    return -1;
	free_reg(st, sr);
    }
#endif
    // dry run (get nvar if any)
    num = d.body.len;
    if (!csp_parse_const_expr(st, &tv[d.body.pos], &num, NULL))
	return -1;
    if (st->nvar) {
	int k;
	cnd2 = alloc_reg(st);
	if (asm_LI(st, cnd2, 0) < 0)
	    return -1;
	for (k = 0; k < st->nvar; k++) {
	    if (asm_mem(st, OP_CHG, cnd2, st->var[k]) < 0)
		return -1;
	}
    }
    // cnd = state condition
    // cnd2 = changed condition
    if ((cnd >= 0) && (cnd2 >= 0)) {
	if (asm_AND(st, cnd, cnd, cnd2) < 0)
	    return -1;
	free_reg(st, cnd2);
    }
    else if (cnd2 >= 0)
	cnd = cnd2;

    if ((num = d.cond.len) > 0) {
	rentry_t rcond;
	// generate condition
	if (!csp_parse_expr(st, &tv[d.cond.pos], &num, &rcond))
	    return -1;
	if (!rcond.L) csp_load(st, &rcond);
	if (cnd < 0) {
	    cnd = alloc_reg(st);
	    if (asm_MOV(st, cnd, rcond.reg) < 0)
		return -1;
	}
	else {
	    if (asm_AND(st, cnd, rcond.reg, cnd) < 0)
		return -1;
	}
	free_reg(st, rcond.reg);
    }
    if (cnd < 0) {
	cnd = alloc_reg(st);	
	if (asm_LI(st, cnd, -1) < 0)
	    return -1;
    }
    num = d.body.len;
    if ((j = csp_new_rule(st, cnd, 0)) < 0)
	return -1;
    free_reg(st, cnd);
    if (!csp_parse_expr(st, &tv[d.body.pos], &num, &rbody))
	return -1;
    if (!rbody.L) csp_load(st, &rbody);
    st->instr[j].r.nxt = st->ps.nn - j;
    if (csp_new_next(st, rbody.reg) < 0)
	return -1;
    if (rbody.L) free_reg(st, rbody.reg);
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
    const tstr_t name = { .ptr = str, .len = len};    
    if ((ix = csp_new_udecl(st, &name, DECL_CAN)) == BAD_INDEX)
	return BAD_INDEX;
    i = INDEX(ix);
    st->decl[i].res = MAKE_RES(1);
    st->decl[i].vt = V_UNSIGNED;
    st->decl[i].dir = DIR_IN;
    st->decl[i].ca.id = idx;
    st->decl[i].ca.bit = p0;
    st->decl[i].ca.len = MAKE_CAN_LEN((p1-p0)+1);
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
    if (csp_load_int(st, cr, c) < 0)
	return -1;
    // load constant k into kr
    kr = alloc_reg(st);
    if (csp_load_int(st, kr, k) < 0)
	return -1;

    // first build condition (can bit test)
    if ((zx = lookup_can_range(st, idx, p0, p0)) == BAD_INDEX) {
	if ((zx = make_can_range(st, NULL, 0, idx, p0, p0)) == BAD_INDEX)
	    return -1;
    }
    // load zx into register
    zr = alloc_reg(st);
    if (asm_mem(st,OP_LD,zr,zx) < 0)
	return -1;

    cnd = alloc_reg(st);
    if (asm_EQEQ(st, cnd, zr, cr) < 0)
	return -1;
    if ((j = csp_new_rule(st, cnd, 0)) < 0)
	return -1;
    if (asm_mem(st, OP_ST, kr, ox) < 0)
	return -1;
    st->instr[j].r.nxt = st->ps.nn - j;
    if (csp_new_next(st, kr) < 0)
	return -1;
    free_reg(st, cr);
    free_reg(st, kr);
    free_reg(st, zr);
    free_reg(st, cnd);
    return 0;
}

// FrameID BytePos Mask OnBits OffBits
NOINLINE int csp_parse_legacy(csp_rt_t* st, token_t* tv, size_t n)
{
    index_t out;
    index_t idx;
    ivalue_t pos;
    ivalue_t mask;
    ivalue_t on_bits;
    ivalue_t off_bits;
    const tstr_t tout = { .ptr = "OUT", .len = 3};    
    int i = 0;

    if (tv[0].t != INT) return -1;
    if (tv[1].t != INT) return -1;
    if (tv[2].t != INT) return -1;
    if (tv[3].t != INT) return -1;
    if (tv[4].t != INT) return -1;

    pos = tv[1].v.val.i;
    mask = tv[2].v.val.i;
    on_bits = tv[3].v.val.i;
    off_bits = tv[4].v.val.i;

    // generate OUT pin if not already exist
    if ((out = lookup_decl(st, &tout)) == BAD_INDEX) {
	// Fixme: Configure pin number etc for standard OUT
	if ((out = csp_new_decl(st,&tout,DECL_DIGITAL)) == BAD_INDEX)
	    return -1;
    }

    if ((idx = lookup_const(st, V_INTEGER, tv[0].v.val)) == BAD_INDEX)
	idx = new_signed_const(st, tv[0].v.val.i);

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
NOINLINE int csp_parse_immediate(csp_rt_t* st, token_t* tv, size_t n)
{
    return 0;
}

#if defined(SUPPORT_STATES) && (SUPPORT_STATES==1)

NOINLINE int lookup_state(csp_rt_t* st, const tstr_t* name)
{
    int i;
    for (i = 0; i < st->ps.ns; i++) {
	int pos = st->states[i].name;
	int len = st->str[pos-1];
	if (len == name->len) {
	    if (memcmp(&st->str[pos], name->ptr, len) == 0)
		return i;
	}
    }
    return -1;
}

NOINLINE int add_state(csp_rt_t* st, const tstr_t* name)
{
    int i;
    if ((i = lookup_string(st, name->ptr, name->len)) < 0) {
	if ((i = new_string(st, name->ptr, name->len)) < 0)
	    return -1;
    }
    if (st->ps.ns < MAX_STATES) {
	int s = st->ps.ns;
	st->states[s].name = i;
	st->states[s].snum = s;
	st->ps.ns++;
#ifdef DEBUG
	if (debug) {
	printf("added state %d %.*s\n", s, st->str[i-1], &st->str[i]);
	}
#endif
	return s;
    }
    csp_set_error(st, ERR_TOO_MANY_STATES);
    return -1;
}

NOINLINE int csp_parse_in(csp_rt_t* st, token_t* tv, size_t n)
{
    int i;
    if (tv[0].t != HASH) return -1;
    if (tv[1].t != IN) return -1;
    if (tv[2].t != WORD) return -1;

    if (st->sdef != -1) {
	csp_set_error(st, ERR_END_MISMATCH);
	return -1;
    }
    if ((i = lookup_state(st, &tv[2].v.str)) < 0) {
	csp_set_err_arg_tstr(st, 0, &tv[2].v.str);
	csp_set_error(st, ERR_STATE_NOT_DECLARED);
	return -1;
    }
    // compile time state, add rules to states[i].snum
    st->sdef = st->states[i].snum;
    return 0;
}

NOINLINE int csp_parse_states(csp_rt_t* st, token_t* tv, size_t n)
{
    int i;
    if (tv[0].t != HASH) return -1;
    if (tv[1].t != STATES) return -1;
    for (i = 2; i < n; i++) {
	int j;
	if (tv[i].t != WORD) return -1;
	if ((j = lookup_state(st, &tv[i].v.str)) >= 0)
	    continue; // already installed (no error maybe warning?)
	if (add_state(st, &tv[i].v.str) < 0)
	    return -1;
    }
    return 0;
}

#endif


NOINLINE int csp_parse(csp_rt_t* st, char* str)
{
    token_t tv[MAX_LINE_TOKENS];
    size_t num = MAX_LINE_TOKENS;
    reg_allocator_t alloc;
    int n;

    st->ap = &alloc;
    while((n = csp_scan_line(str, tv, &num)) > 0) {
	int r = -1;
	str += n;
	alloc_init(st->ap);

	if (tv[0].t == NEWLINE)
	    r = 0;
	else if (tv[0].t == INT && tv[1].t == INT) {
	    r = csp_parse_legacy(st, tv, num);
	}
	else if (tv[0].t == HASH) {
	    switch(tv[1].t) {
	    case MODULE:  // '#' 'module' WORD
		r = csp_parse_module(st, tv, num);
		break;
#if defined(SUPPORT_STATES) && (SUPPORT_STATES==1)
	    case IN:     // '#' 'in' WORD
		r = csp_parse_in(st, tv, num);
		break;
	    case STATES:  // '#' 'states' WORD ... WORD
		r = csp_parse_states(st, tv, num);
		break;
#endif
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

    st->dio[DIN] = st->dio[DOUT] = st->dv0;
    st->reactive = reactive;
    if ((st->transaction = transaction) != 0) {
#if defined(SUPPORT_TRANSACTION) && (SUPPORT_TRANSACTION==1)
	st->dio[DOUT] = st->dv1;
#endif
    }
    st->ps.nn = 0;
    st->ps.nd = 0;
    st->ps.nq = 0;
    st->ps.strp = 1;
    st->ps.err_strp = MAX_STR_BUF;
    st->ps.err  = ERR_OK;
    st->ps.err_args[0] = st->ps.err_args[1] = st->ps.err_args[2] = 0;
    st->ps.line = 0;

    st->nt = 0;
    st->ni = 0;
    st->no = 0;
    st->nm = 0;
    st->cur = 0;      // current module = global
    st->mdef = BAD_INDEX;  // no module being defined
    st->var = st->timer;  // reuse timer[] for var list during <- parse
    st->nvar = 0;
    st->rimp = 0;

    st->str[0] = 0;  // reserved 0 and nil
    st->ufuncs = NULL;
    st->num_ufuncs = 0;
    st->uconst = NULL;
#if defined(SUPPORT_STATES) && (SUPPORT_STATES==1)
    {
	const tstr_t State = { .ptr = "State", .len = 5};
	const tstr_t INIT  = { .ptr = "INIT", .len = 4};
	const tstr_t NORMAL = { .ptr = "NORMAL", .len = 6};
	st->ps.ns = 0;  // install INIT (cycle()==0) and NORMAL
	st->sx = csp_new_decl(st, &State, DECL_VARIABLE);
	st->sdef = -1;
	// add state INIT=0 and NORMAL=1
	if (add_state(st, &INIT) != 0) {
#ifdef DEBUG
	    if (debug) {
	    printf("unabled to add INIT state=0\n");
	    }
#endif
	    return -1;
	}
	if (add_state(st, &NORMAL) != 1) {
#ifdef DEBUG
	    if (debug) {	    
	    printf("unabled to add NORMAL state=1\n");
	    }
#endif
	    return -1;
	}	
    }
#endif    
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

// copy config data to value slot config
NOINLINE static void setup_timer(csp_rt_t* st, index_t ix)
{
    value_t* iptr;
    value_t* optr;
    int m = OBJ(ix);
    csp_decl_t* dptr = &st->decl[INDEX(ix)];
    index_t tx = MAKE_INDEX(m, INDEX(ix+1));

    // clear timeout flag
    csp_dio_slots(st, ix, &iptr, &optr);
    iptr->t.fired = optr->t.fired = 0;
    iptr->t.val = optr->t.val = dptr->tm.init;
    iptr->t.running = optr->t.running = dptr->tm.init;
    iptr->t.period = optr->t.period = dptr->tm.period;
    
    // load current time+1
    csp_dio_slots(st, tx, &iptr, &optr);
    if (dptr->tm.init) {
	iptr->u = optr->u = csp_time_ms();
    }
    else 
	iptr->u = optr->u = 0;
}


// copy config data to value slot config
NOINLINE static void setup_analog(csp_rt_t* st, index_t ix)
{
    value_t* iptr;
    value_t* optr;
    csp_decl_t* dptr = &st->decl[INDEX(ix)];
    
    csp_dio_slots(st, ix, &iptr, &optr);
    iptr->a.dir  = optr->a.dir     = dptr->dir;
    iptr->a.pin  = optr->a.pin     = dptr->an.pin;
    iptr->a.port = optr->a.port    = dptr->an.port;
    iptr->a.pwm  = optr->a.pwm     = dptr->an.pwm;
    iptr->a.endian = optr->a.endian = dptr->an.endian;
}

// copy config data to value slot config
NOINLINE static void setup_digital(csp_rt_t* st, index_t ix)
{
    value_t* iptr;
    value_t* optr;
    csp_decl_t* dptr = &st->decl[INDEX(ix)];
    
    csp_dio_slots(st, ix, &iptr, &optr);    
    iptr->d.dir  = optr->d.pin = dptr->dir;	    
    iptr->d.pin  = optr->d.pin = dptr->di.pin;
    iptr->d.port = optr->d.pin = dptr->di.port;
    iptr->d.pullup = optr->d.pullup = dptr->di.pullup;
    iptr->d.pulldown = optr->d.pulldown = dptr->di.pulldown;    
}

// copy config data to value slot config
NOINLINE static void setup_can(csp_rt_t* st, index_t ix)
{
}

NOINLINE static void add_io(csp_rt_t* st, index_t ix)
{
    int i = INDEX(ix);
    if (st->decl[i].dir & DIR_IN) {
	if (st->ni < MAX_INPUTS) // warning?
	    st->input[st->ni++] = ix;
    }
    if (st->decl[i].dir & DIR_OUT) {
	if (st->no < MAX_OUTPUTS) // warning
	    st->output[st->no++] = ix;
    }
}

// copy constant and init values
// setup input, output and timer lists
//
int csp_rt_start(csp_rt_t* st)
{
    int i;
    int offs;
    int in_module = 0;
    value_t* iptr;
    value_t* optr;

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
	    in_module=1;
	    if (st->nm < MAX_MODULES)
		st->module[st->nm++] = ix;
	    break;
	case DECL_END:
	    in_module = 0;
	    break;
	case DECL_OBJECT:
	    // Per-object init done after offs[] is allocated
	    break;
	case DECL_CONSTANT:
	    // global or "template" version
	    csp_dio_slots(st, ix, &iptr, &optr);
	    *iptr = *optr = st->decl[i].cn.init;
	    break;
	case DECL_VARIABLE:
	    // global or "template" version
	    csp_dio_slots(st, ix, &iptr, &optr);
	    *iptr = *optr = st->decl[i].va.init;
	    break;
	case DECL_TIMER:
	    if (!in_module) {
		setup_timer(st, ix);
		st->timer[st->nt++] = ix;
	    }
	    break;
	    
	case DECL_DIGITAL:
	    if (!in_module) {
		setup_digital(st, ix);
		add_io(st, ix);
	    }
	    break;
	    
	case DECL_ANALOG:
	    if (!in_module) {
		setup_analog(st, ix);
		add_io(st, ix);
	    }
	    break;	    
	    
	case DECL_CAN:
	    if (!in_module) {
		setup_can(st, ix);
		add_io(st, ix);
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
	if (offs > MAX_DECLS) {
	    // when objects are included
	    csp_set_error(st, ERR_TOO_MANY_DECLARATIONS);
	    return -1;
	}
    }
    // init per-object data (after offs[] is set)
    for (i = 0; i < st->ps.nq; i++) {
	int m = i+1;
	int j;
	index_t ix = st->object[m];
	index_t mx = st->decl[INDEX(ix)].mq.mx; // module def
	ivalue_t dn = st->decl[INDEX(mx)].md.n;  // number of decl elements
	
	int base = INDEX(mx)+1;
	for (j = 0; j < dn; j++) {
	    int dj = base + j;         // decl index
	    index_t fx = MAKE_INDEX(m,dj); // field index
#ifdef DEBUG
	    if (debug) {	    
	    printf("init OBJECT %s, FIELD %s[%d]\n",
		   decl_name(st, ix), decl_name(st, fx), dj);
	    }
#endif
	    switch (st->decl[dj].type) {
	    case DECL_CONSTANT:
		csp_dio_slots(st, fx, &iptr, &optr);
		*iptr = *optr = st->decl[dj].cn.init;
		break;
	    case DECL_VARIABLE:
		csp_dio_slots(st, fx, &iptr, &optr);
		*iptr = *optr = st->decl[dj].va.init;
		break;
	    case DECL_TIMER:
		setup_timer(st, fx);
		st->timer[st->nt++] = fx;
		break;
	    case DECL_DIGITAL:
		setup_digital(st, fx);
		add_io(st, fx);
		break;
	    case DECL_ANALOG:
		setup_analog(st, fx);
		add_io(st, fx);
		break;
	    case DECL_CAN:
		setup_can(st, ix);
		add_io(st, ix);		
	    default:
		break;
	    }
	}
    }
    st->cycle = 0;  // init trace shows cycle 0
    return 0;
}

int csp_set_transaction(csp_rt_t* st, int onoff)
{
#if defined(SUPPORT_TRANSACTION) && (SUPPORT_TRANSACTION==1)
    if ((st->transaction = onoff) != 0) {
	if (st->dio[DIN] == st->dv0)
	    st->dio[DOUT] = st->dv1;
	else
	    st->dio[DOUT] = st->dv0;
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

int csp_set_latch(csp_rt_t* st, int onoff)
{
    int old_value = st->latch;
    st->latch = onoff;
    return old_value;
}

// ============================================================
// Interactive command handling
// ============================================================

static int cmd_help(csp_rt_t* st, const char* args);
static int cmd_list(csp_rt_t* st, const char* args);
static int cmd_state(csp_rt_t* st, const char* args);
static int cmd_reset(csp_rt_t* st, const char* args);
static int cmd_commit(csp_rt_t* st, const char* args);
static int cmd_quit(csp_rt_t* st, const char* args);
static int cmd_latch(csp_rt_t* st, const char* args);

static const csp_cmd_t builtin_cmds[] = {
    { "help",   "Show this help",          cmd_help },
    { "?",      NULL,                      cmd_help },
    { "list",   "List declarations",       cmd_list },
    { "state",  "Show current values",     cmd_state },
    { "reset",  "Reset to initial values", cmd_reset },
    { "latch",  "on or off, device output", cmd_latch },
    { "commit", "Commit pending values",   cmd_commit },
    { "save",   "Save state to storage",   csp_cmd_save },
    { "load",   "Load state from storage", csp_cmd_load },
    { "quit",   "Exit interactive mode",   cmd_quit },
    { "exit",   NULL,                      cmd_quit },
    { NULL, NULL, NULL }
};

static int cmd_help(csp_rt_t* st, const char* args)
{
    (void)st; (void)args;
    csp_print_str("Commands:\n");
    for (const csp_cmd_t* c = builtin_cmds; c->name; c++) {
	if (c->help) {
	    csp_print_str("  /");
	    csp_print_str(c->name);
	    int len = strlen(c->name);
	    while (len++ < 10) csp_print_char(' ');
	    csp_print_str(c->help);
	    csp_print_char('\n');
	}
    }
    csp_print_str("\nSyntax:\n");
    csp_print_str("  #variable X integer    Declare variable\n");
    csp_print_str("  X = Y + 1              Rule (always)\n");
    csp_print_str("  X = Y + 1 ? cond       Rule (conditional)\n");
    csp_print_str("  > X + 1                Evaluate expression\n");
    csp_print_str("  > X = 5                Assign value\n");
    return CSP_CMD_OK;
}

static int cmd_list(csp_rt_t* st, const char* args)
{
    (void)args;
    for (int i = 0; i < st->ps.nd; i++) {
	if (st->decl[i].type == DECL_END) break;
	if (st->decl[i].type == DECL_MODULE) continue;
	const char* name = decl_name(st, MAKE_INDEX(0, i));
	if (!name || !*name) continue;

	csp_print_str(name);
	csp_print_str(" : ");
	switch (st->decl[i].type) {
	case DECL_VARIABLE:
	    csp_print_str(csp_fmt_vtype(st->decl[i].vt));
	    csp_print_str(" = ");
	    csp_print_value(st, st->decl[i].vt,
			   csp_value(st, MAKE_INDEX(0, i)));
	    break;
	case DECL_CONSTANT:
	    csp_print_str("const ");
	    csp_print_str(csp_fmt_vtype(st->decl[i].vt));
	    csp_print_str(" = ");
	    csp_print_value(st, st->decl[i].vt, st->decl[i].cn.init);
	    break;
	case DECL_TIMER:
	    csp_print_str("timer");
	    break;
	case DECL_DIGITAL:
	    csp_print_str("digital ");
	    csp_print_str(csp_fmt_pindir(st->decl[i].dir));
	    break;
	case DECL_ANALOG:
	    csp_print_str("analog ");
	    csp_print_str(csp_fmt_pindir(st->decl[i].dir));
	    break;
	default:
	    break;
	}
	csp_print_char('\n');
    }
    return CSP_CMD_OK;
}

static int cmd_state(csp_rt_t* st, const char* args)
{
    (void)args;
    csp_print_str("latch = ");
    csp_print_str(st->latch ? "on" : "off");
    csp_print_char('\n');

    for (int i = 0; i < st->ps.nd; i++) {
	if (st->decl[i].type == DECL_END) break;
	if (st->decl[i].type != DECL_VARIABLE) continue;
	const char* name = decl_name(st, MAKE_INDEX(0, i));
	if (!name || !*name) continue;

	csp_print_str(name);
	csp_print_str(" = ");
	csp_print_value(st, st->decl[i].vt,
		       csp_value(st, MAKE_INDEX(0, i)));
	csp_print_char('\n');
    }
    return CSP_CMD_OK;
}

static int cmd_reset(csp_rt_t* st, const char* args)
{
    (void)args;
    csp_rt_start(st);
    csp_setup(st);
    csp_print_str("Reset\n");
    return CSP_CMD_OK;
}

static int cmd_commit(csp_rt_t* st, const char* args)
{
    (void)args;
    csp_commit(st);
    csp_print_str("Committed\n");
    return CSP_CMD_OK;
}

static int cmd_quit(csp_rt_t* st, const char* args)
{
    (void)st; (void)args;
    return CSP_CMD_QUIT;
}

void csp_cmd_help(void)
{
    cmd_help(NULL, NULL);
}

static int cmd_latch(csp_rt_t* st, const char* args)
{
    int latch = 0;
    if (strcmp(args, "on") == 0)
	latch = 1;
    else if (strcmp(args, "off") == 0)
	latch = 0;
    else
	return CSP_CMD_ERROR;
    csp_set_latch(st, latch);
    return CSP_CMD_OK;
}

int csp_cmd_dispatch(csp_rt_t* st, const char* cmd)
{
    const char* args = cmd;
    const csp_cmd_t* c;

    // Skip command name to find args
    while (*args && *args != ' ' && *args != '\t') args++;
    int namelen = args - cmd;
    while (*args == ' ' || *args == '\t') args++;

    for (c = builtin_cmds; c->name; c++) {
	if (strncmp(cmd, c->name, namelen) == 0 &&
	    c->name[namelen] == '\0') {
	    return c->fn(st, args);
	}
    }
    return CSP_CMD_NOTFOUND;
}

// Process immediate expression (> expr or > var = expr)
static int csp_process_immediate(csp_rt_t* st, char* line)
{
    token_t tv[MAX_LINE_TOKENS];
    size_t num = MAX_LINE_TOKENS;
    reg_allocator_t* saved_ap;
    rentry_t result;

    if (csp_scan_line(line, tv, &num) < 0) {
	csp_print_str("Scan error\n");
	return -1;
    }
    if (num == 0 || tv[0].t == NEWLINE)
	return 0;

    saved_ap = st->ap;
    st->ap = NULL;
    st->ev = 1; // eval variables during (compile)
    if (!csp_parse_expr(st, tv, &num, &result)) {
	st->ap = saved_ap;
	csp_print_str("Error: ");
	csp_print_str(csp_format_error(st->ps.err));
	csp_print_char('\n');
	csp_clr_error(st);
	return -1;
    }
    st->ap = saved_ap;
    st->ev = 0;

    if (result.I)
	csp_print_value(st, result.vt, result.val);
    else if (result.ix != BAD_INDEX)
	csp_print_value(st, result.vt, csp_value(st, result.ix));
    else
	csp_print_str("NONE");
    csp_println();
    return 0;
}

// Process persistent definition (# declaration or rule)
static int csp_process_persistent(csp_rt_t* st, char* line)
{
    if (csp_parse(st, line) < 0) {
	csp_print_str("Error: ");
	csp_print_str(csp_format_error(st->ps.err));
	csp_print_char('\n');
	csp_clr_error(st);
	return -1;
    }
    csp_rt_start(st);
    csp_setup(st);
    csp_print_str("OK\n");
    return 0;
}

int csp_process_line(csp_rt_t* st, char* line)
{
    // Skip leading whitespace
    while (*line && (*line == ' ' || *line == '\t')) line++;
    if (*line == '\0' || *line == '\n')
	return CSP_CMD_OK;

    // Remove trailing newline
    int len = strlen(line);
    if (len > 0 && line[len-1] == '\n') line[len-1] = '\0';

    if (*line == '/') {
	// Command
	int r = csp_cmd_dispatch(st, line + 1);
	if (r == CSP_CMD_NOTFOUND) {
	    csp_print_str("Unknown command: ");
	    csp_print_str(line);
	    csp_print_str(" (try /help)\n");
	}
	return r;
    }
    else if (*line == '#') {
	// Persistent definition
	csp_process_persistent(st, line);
	return CSP_CMD_OK;
    }
    else if (*line == '>') {
	// Immediate expression
	csp_process_immediate(st, line + 1);
	return CSP_CMD_OK;
    }
    else {
	// Try as immediate expression (backwards compat)
	csp_process_immediate(st, line);
	return CSP_CMD_OK;
    }
}

// ============================================================
// Line input handling (shared between platforms)
// ============================================================

char csp_line_buf[CSP_LINE_BUF_SIZE];
uint8_t csp_line_pos = 0;
uint8_t csp_line_ready = 0;
static uint8_t need_prompt = 1;

void csp_line_init(void)
{
    csp_line_pos = 0;
    csp_line_ready = 0;
    need_prompt = 1;
}

void csp_line_prompt(void)
{
    if (need_prompt) {
	csp_print_str("> ");
	csp_flush();
	need_prompt = 0;
    }
}

void csp_line_input(char c)
{
    if (c == '\n' || c == '\r') {
	if (csp_line_pos > 0) {
	    csp_line_buf[csp_line_pos] = '\0';
	    csp_line_ready = 1;
	}
	csp_print_str("\r\n");
	csp_flush();
	need_prompt = 1;
    }
    else if (c == '\b' || c == 127) {
	if (csp_line_pos == 0) {
	    csp_print_char('\a');
	} else {
	    csp_line_pos--;
	    csp_print_str("\b \b");
	}
	csp_flush();
    }
    else if (c == 21) { // Ctrl-U: clear line
	while (csp_line_pos > 0) {
	    csp_line_pos--;
	    csp_print_str("\b \b");
	}
	csp_flush();
    }
    else if (c >= 32 && c < 127 && csp_line_pos < CSP_LINE_BUF_SIZE - 1) {
	csp_line_buf[csp_line_pos++] = c;
	csp_print_char(c);
	csp_flush();
    }
}

// Common timer input (called from cs_input)

void csp_input_timer(csp_rt_t* st)
{
    int i;
    uvalue_t now_ms;

    now_ms = csp_time_ms();
    for (i = 0; i < st->nt; i++) {
	index_t ix = st->timer[i];
	int obj = OBJ(ix);
	// Replace CURRENT with actual object
	value_t* iptr;
	value_t* optr;	

	csp_dio_slots(st, ix, &iptr, &optr);
	iptr->t.fired = optr->t.fired = 0;
	// tx value: 0=stopped, >0=running (start_time+1)
	if (iptr->t.running) {
	    index_t tx = MAKE_INDEX(obj, INDEX(ix+1));
	    value_t* txptr = csp_dio_slot(st, tx, DIN);
	    uvalue_t t0 = txptr->u;
	    if ((now_ms - t0) >= iptr->t.period) {
		iptr->t.running = optr->t.running = 0; // not running
		iptr->t.val = optr->t.val = 0;         // off
		iptr->t.fired = optr->t.fired = 1;     // fired
#if defined(SUPPORT_REACTIVE) && (SUPPORT_REACTIVE==1)
		if (st->reactive) {
		    csp_enq_elist(st, ix);
		}
#endif
	    }
	}
    }
}

// Expect current value to be in din (after commit)
void csp_output_timer(csp_rt_t* st)
{
    int i;
    uint32_t now_ms;
    uint32_t wait_ms = NOTIMEOUT;

    now_ms = csp_time_ms();
    for (i = 0; i < st->nt; ++i) {
	index_t ix = st->timer[i];
	int obj = OBJ(ix);
	value_t* iptr;
	value_t* optr;
	index_t tx = MAKE_INDEX(obj, INDEX(ix+1));
	
	csp_dio_slots(st, ix, &iptr, &optr);	

	if (iptr->t.running) {
	    // running - calculate wait time (take minimum)
	    uvalue_t t0 = csp_dio_slot(st, tx, DIN)->u;
	    uvalue_t period = iptr->t.period;
	    uint32_t dt = (now_ms - t0);
	    uint32_t w = (dt >= period) ? 0 : (period - dt);
	    if (w < wait_ms)
		wait_ms = w;
	}
	else {
	    // stopped - check if start requested
	    if (iptr->t.val) {
		index_t tx = MAKE_INDEX(obj, INDEX(ix+1));
		uvalue_t period = iptr->t.period;
		uint32_t dt = period;
		
		iptr->t.running = optr->t.running = 1;
		iptr->t.fired = optr->t.fired = 0;

		csp_dio_slots(st, tx, &iptr, &optr);
		iptr->u = optr->u = now_ms;

		if (dt < wait_ms)
		    wait_ms = dt;
	    }
	}
    }
    st->wait_ms = wait_ms;
}
