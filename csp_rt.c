// CandySpeak runtime
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "csp.h"
#ifdef DEBUG
#include <stdio.h>
#include "csp_format.h"
#endif

// Prevent inlining of large parse functions to reduce code size
#ifdef __GNUC__
#define NOINLINE __attribute__((noinline))
#else
#define NOINLINE
#endif

#define CAT_HELPER2(x,y) x ## y
#define CAT2(x,y) CAT_HELPER2(x,y)

// convert integer to -1 if y != 0  0 otherwise
#define BOOL(y) (-((y)!=0))

// CTYPE
/*
#include <ctype.h>
#define ISDIGIT(c)  isdigit((c))
#define ISXDIGIT(c) isxdigit((c))
#define ISALPHA(c)  isalpha((c))
*/

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
// func

// string length for constant strings "foo" => 3
#define CSTRLEN(str) (sizeof((str))-1)

#define TOK_ENT(o,c,n) \
    [(o)] = { .tok=(o),.code=(c),.name=(n),.namelen=CSTRLEN((n)),.arity=-1,.prec=-1,.assoc=NO }

#define INSTR_ENT(o,c,n,a,p,s) \
    [(o)] = { .tok=(o),.code=(c),.name=(n),.namelen=CSTRLEN((n)),.arity=(a),.prec=(p),.assoc=(s) }

#define DECL_ENT(o,c,n) \
    [(o)] = { .tok=(o),.code=(c),.name=(n),.namelen=CSTRLEN((n)),.arity=-1,.prec=-1,.assoc=NO }

static const char s_null[] RODATA = "";
static const char s_module[] RODATA = "module";
static const char s_end[] RODATA = "end";
static const char s_constant[] RODATA = "constant";
static const char s_variable[] RODATA = "variable";
static const char s_digital[] RODATA = "digital";
static const char s_analog[] RODATA = "analog";
static const char s_timer[] RODATA = "timer";
static const char s_can[] RODATA = "can";
static const char s_EXCLAMATION[] RODATA = "!";
static const char s_TILDE[] RODATA = "~";
static const char s_MINUS[] RODATA = "-";
static const char s_PLUS[] RODATA = "+";
static const char s_ASTERISK[] RODATA = "*";
static const char s_SLASH[] RODATA = "/";
static const char s_PERCENT[] RODATA = "%";
static const char s_LTLT[] RODATA = "<<";
static const char s_GTGT[] RODATA = ">>";
static const char s_LT[] RODATA = "<";
static const char s_LTEQ[] RODATA = "<=";
static const char s_RIMP[] RODATA = "<-";
static const char s_GT[] RODATA = ">";
static const char s_GTEQ[] RODATA = ">=";
static const char s_EQEQ[] RODATA = "==";
static const char s_NEQ[] RODATA = "!=";
static const char s_AMP[] RODATA = "&";
static const char s_CIRC[] RODATA = "^";
static const char s_BAR[] RODATA = "|";
static const char s_AMPAMP[] RODATA = "&&";
static const char s_BARBAR[] RODATA = "||";
static const char s_EQ[] RODATA = "=";
static const char s_COMMA[] RODATA = ",";
static const char s_QUEST[] RODATA = "?";
static const char s_next[] RODATA = "next";
static const char s_enter[] RODATA = "enter";
static const char s_leave[] RODATA = "leave";
static const char s_new[] RODATA = "new";
static const char s_call[] RODATA = "call";
static const char s_ld[] RODATA = "ld";
static const char s_st[] RODATA = "st";
static const char s_li[] RODATA = "li";
static const char s_arg[] RODATA = "arg";
static const char s_cvtif[] RODATA = "cvtif";
static const char s_cvtfi[] RODATA = "cvtfi";
static const char s_pullup[] RODATA = "pullup";
static const char s_pulldown[] RODATA = "pulldown";
static const char s_resolution[] RODATA = "resolution";
static const char s_in[] RODATA = "in";
static const char s_out[] RODATA = "out";
static const char s_inout[] RODATA = "inout";
static const char s_pwm[] RODATA = "pwm";
static const char s_float[] RODATA = "float";
static const char s_integer[] RODATA = "integer";
static const char s_unsigned[] RODATA = "unsigned";
static const char s_string[] RODATA = "string";
static const char s_little[] RODATA = "little";
static const char s_big[] RODATA = "big";
static const char s_LP[] RODATA = "(";
static const char s_RP[] RODATA = ")";
static const char s_HASH[] RODATA = "#";
static const char s_DOT[] RODATA = ".";
static const char s_COLON[] RODATA = ":";
static const char s_LB[] RODATA = "[";
static const char s_RB[] RODATA = "]";

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
    INSTR_ENT(PLUS1,OP_POS,s_PLUS,1,105,RIGHT),
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
    INSTR_ENT(LDI,OP_LI,s_li,-1,-1,NO),
    INSTR_ENT(ARG,OP_ARG,s_arg,-1,-1,NO),
    INSTR_ENT(CVTIF,OP_CVTIF,s_cvtif,-1,-1,NO),
    INSTR_ENT(CVTIF,OP_CVTFI,s_cvtfi,-1,-1,NO),
    
    // keywords
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

// Float/fixpoint operations - conditional on FVALUE_IS_FIXPOINT
#if FVALUE_IS_FIXPOINT
#define op_FADD(y, z)  FIX_ADD((y), (z))
#define op_FSUB(y, z)  FIX_SUB((y), (z))
#define op_FMUL(y, z)  FIX_MUL((y), (z))
#define op_FDIV(y, z)  FIX_DIV((y), (z))
#define op_FLT(y, z)   (-FIX_LT((y), (z)))
#define op_FLTE(y, z)  (-FIX_LTE((y), (z)))
#define op_FGT(y, z)   (-FIX_GT((y), (z)))
#define op_FGTE(y, z)  (-FIX_GTE((y), (z)))
#define op_FEQEQ(y, z) (-FIX_EQ((y), (z)))
#define op_FNEQ(y, z)  (-FIX_NEQ((y), (z)))
#define op_FNEG(y)     FIX_NEG(y)
#define op_FPOS(y)     (y)
#define op_CVTIF(y)    FIX_FROM_INT(y)
#define op_CVTFI(y)    FIX_TO_INT(y)
#else
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
#define op_FNEG(y)     (-(y))
#define op_FPOS(y)     (y)
#define op_CVTIF(y)    ((fvalue_t)(y))
#define op_CVTFI(y)    ((ivalue_t)(y))
#endif

#define op_NOT(y)  (~BOOL((y)))
#define op_NEG(y)  (-(y))
#define op_POS(y)  (y)
#define op_BNOT(y)  (~(y))

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

static const char s_ADD[] RODATA = "ADD";
static const char s_SUB[] RODATA = "SUB";
static const char s_MUL[] RODATA = "MUL";
static const char s_DIV[] RODATA = "DIV";
static const char s_REM[] RODATA = "REM";
static const char s_SLA[] RODATA = "SLA";
static const char s_SRA[] RODATA = "SRA";
static const char s_BAND[] RODATA = "BAND";
static const char s_BOR[] RODATA = "BOR";
static const char s_BXOR[] RODATA = "BXOR";
static const char s_AND[] RODATA = "AND";
static const char s_OR[] RODATA = "OR";
static const char s_ASSIGN[] RODATA = "ASSIGN";
static const char ss_LT[] RODATA = "LT";
static const char ss_LTE[] RODATA = "LTE";
static const char ss_GT[] RODATA = "GT";
static const char ss_GTE[] RODATA = "GTE";
static const char ss_EQ[] RODATA = "EQ";
static const char ss_NEQ[] RODATA = "NEQ";
static const char s_BNOT[] RODATA = "BNOT";
static const char s_NEG[] RODATA = "NEG";
static const char s_POS[] RODATA = "POS";
static const char s_NOT[] RODATA = "NOT";
static const char s_CVTIF[] RODATA = "CVTIF";
static const char s_CVTFI[] RODATA = "CVTFI";

static const char s_FADD[] RODATA = "FADD";
static const char s_FSUB[] RODATA = "FSUB";
static const char s_FMUL[] RODATA = "FMUL";
static const char s_FDIV[] RODATA = "FDIV";
static const char s_FNEG[] RODATA = "FNEG";

static const char s_FLT[] RODATA = "FLT";
static const char s_FLTE[] RODATA = "FLTE";
static const char s_FGT[] RODATA = "FGT";
static const char s_FGTE[] RODATA = "FGTE";
static const char s_FEQ[] RODATA = "FEQ";
static const char s_FNEQ[] RODATA = "FNEQ";

static const char ss_COMMA[] RODATA = "COMMA";

static const char s_ENTER[] RODATA = "ENTER";
static const char s_LEAVE[] RODATA = "LEAVE";
static const char s_NEW[] RODATA = "NEW";
static const char s_LI[] RODATA = "LI";
static const char s_LIU[] RODATA = "LIU";
static const char s_LIH[] RODATA = "LIH";
static const char s_ARG[] RODATA = "ARG";
static const char s_ST[] RODATA = "ST";
static const char s_LD[] RODATA = "LD";
static const char s_CALL[] RODATA = "CALL";
static const char s_RULE[] RODATA = "RULE";
static const char s_NEXT[] RODATA = "NEXT";
static const char s_NOP[] RODATA = "NOP";

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
    [OP_POS] = {s_POS,1,V_INTEGER,{V_INTEGER}},
    [OP_NOT] = {s_NOT,1,V_INTEGER,{V_INTEGER}},
    [OP_CVTIF] = {s_CVTIF,1,V_FLOAT,{V_INTEGER}},   // int→float
    [OP_CVTFI] = {s_CVTFI,1,V_INTEGER,{V_FLOAT}},  // float→int

    [OP_FNEG] = {s_FNEG,1,V_FLOAT,{V_FLOAT}},    
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
    [OP_LD]    = {s_LD,0,V_VOID,{}},
    [OP_CALL]  = {s_CALL,0,V_VOID,{}},
    [OP_RULE]  = {s_RULE,0,V_VOID,{}},
    [OP_NEXT]  = {s_NEXT,0,V_VOID,{}},
    [OP_NOP] = {s_NOP,0,V_VOID,{}},    
    
};

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
    case OP_POS:  return f_POS(y);
    case OP_NOT:  return f_NOT(y);
    case OP_CVTIF: return f_CVTIF(y);
    case OP_CVTFI: return f_CVTFI(y);
    case OP_FNEG:  return f_FNEG(y);
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

const char* csp_opcode_name(opcode_t op)
{
    return (const char*) RD_PTR(&info_tab[op].name);
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
    default: return csp_print_str("???");
    }
}

#ifdef DEBUG
void print_rentry(csp_rt_t* st, char* name, rentry_t* rp)
{
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

static value_t fn_timeout(csp_rt_t* st,uint16_t type,
			  value_t* args, uint8_t nargs)
{
    value_t ret;
    index_t ty = args[0].u;
    // int i = st_index(st, ty); fixme. fix object timers!!!
    ret.i = BOOL(!st->decl[INDEX(ty)].tm.running);
    return ret;
}

static value_t fn_changed(csp_rt_t* st,uint16_t type,
			  value_t* args, uint8_t nargs)
{
    value_t ret;
    index_t ty = args[0].u;
    int i = st_index(st, ty);
    ret.i = BOOL(bitset_tst(st->dset, i));
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

static const char s_min[] RODATA = "min";
static const char s_max[] RODATA = "max";
static const char s_abs[] RODATA = "abs";
static const char s_fabs[] RODATA = "fabs";
static const char s_sign[] RODATA = "sign";
static const char s_clip[] RODATA = "clip";
static const char s_timeout[] RODATA = "timeout";
static const char s_changed[] RODATA = "changed";
static const char s_print[] RODATA = "print";
static const char s_println[] RODATA = "println";
static const char s_tick[] RODATA = "tick";
static const char s_cycle[] RODATA = "cycle";

#define CSP_FUNC_ENT(str, a, p, rt, args, f)	\
    {.name=(str),.namelen=sizeof((str))-1,.arity=(a),.pure=(p),	\
	    .rtype=(rt),.argtypes=(args),.fn=(f)}

// Built-in function table
// { name, namelen, nargs, rtype, {argtypes}, fn }
const csp_func_t csp_builtin_funcs[] RODATA = {
    CSP_FUNC_ENT(s_min,     2, 1, V_INTEGER, MAKE_TYPE2(V_INTEGER,V_INTEGER), fn_min ),
    CSP_FUNC_ENT(s_max,     2, 1, V_INTEGER, MAKE_TYPE2(V_INTEGER,V_INTEGER), fn_max ),
    CSP_FUNC_ENT(s_abs,     1, 1, V_INTEGER, MAKE_TYPE1(V_INTEGER), fn_abs ),
    CSP_FUNC_ENT(s_fabs,    1, 1, V_FLOAT,   MAKE_TYPE1(V_FLOAT),   fn_fabs ),
    CSP_FUNC_ENT(s_sign,    1, 1, V_INTEGER, MAKE_TYPE1(V_NUMBER),  fn_sign ),
    CSP_FUNC_ENT(s_clip,    3, 1, V_INTEGER, MAKE_TYPE3(V_INTEGER,V_INTEGER,V_INTEGER), fn_clip),
    CSP_FUNC_ENT(s_timeout, 1, 0, V_INTEGER, MAKE_TYPE1(V_INDEX), fn_timeout),
    CSP_FUNC_ENT(s_changed, 1, 0, V_INTEGER, MAKE_TYPE1(V_INDEX), fn_changed),    
    CSP_FUNC_ENT(s_print,   1, 0, V_INTEGER, MAKE_TYPE1(V_ANY),  fn_print),
    CSP_FUNC_ENT(s_println, 1, 0, V_INTEGER, MAKE_TYPE1(V_ANY),  fn_println),
    CSP_FUNC_ENT(s_tick,    0, 0, V_INTEGER, MAKE_TYPE0(),       fn_tick),
    CSP_FUNC_ENT(s_cycle,   0, 0, V_INTEGER, MAKE_TYPE0(),       fn_cycle),
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
    uint8_t argtypes = RD_WORD(&fn->argtypes);
    return (argtypes >> 4*j) & 0xf;
}

// match function template this code assumes type coerce int->flt
// flt->int. the goal is to match BEST? function to use

int csp_match_args(const csp_func_t* fn, int arity, rentry_t* rarg)
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
	    return 0;
	case V_INTEGER:
	    if (argvt == V_INTEGER) break;
	    if (argvt == V_FLOAT) break;    // coerce!
	    return 0;
	case V_FLOAT:
	    if (argvt == V_FLOAT) break;
	    if (argvt == V_INTEGER) break;  // coerce!
	    return 0;
	case V_STRING:
	    if (argvt == V_STRING) break;
	    return 0;
	case V_INDEX:
	    if (arg.X) break;
	    return 0;
	default:
	    return 0;
	}
    }
    return 1;
}

static int csp_match_fn(const csp_func_t* fn, int num,
			const char* name, uint8_t namelen,
			uint8_t arity, rentry_t* rarg)
{
    int i;

    for (i = 0; i < num; i++) {
	uint8_t roarity = func_arity(fn, i);
	uint8_t ronamelen = func_namelen(fn, i);
	if ((roarity == arity) && (ronamelen == namelen)) {
	    const char* roname = RD_PTR(&fn[i].name); 
	    if (MEMCMP_RD(name, roname, namelen) == 0) {
		if (csp_match_args(&fn[i], arity, rarg))
		    return i;
	    }
	}
    }
    return -1;
}

const csp_func_t* csp_match_func(csp_rt_t* st,
				 const char* name, uint8_t namelen,
				 uint8_t arity, rentry_t* rarg,
				 int* is_user, int* func_idx)
{
    int idx;

    if (st->ufuncs) {
	if ((idx = csp_match_fn(st->ufuncs, st->num_ufuncs,
				name, namelen, arity, rarg)) >= 0) {
	    *is_user = 1;
	    *func_idx = idx;
	    return &st->ufuncs[idx];
	}
    }
    if ((idx = csp_match_fn(csp_builtin_funcs, csp_num_builtin_funcs,
			    name, namelen, arity, rarg)) >= 0) {
	*is_user = 0;
	*func_idx = idx;
	return &csp_builtin_funcs[idx];
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
void csp_enq_elist(csp_rt_t* st, index_t x)
{
#if defined(SUPPORT_REACTIVE) && (SUPPORT_REACTIVE==1)    
    int i;
    index_t ix = INDEX(x);
    index_t base = st->ofs[ix];
    for (i = 0; i < st->idg[ix]; i++) {
	index_t p = st->edg[base+i];  // parent node
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
static index_t lookup_decl_in(csp_rt_t* st, char* name, int name_len,
			      int start, int stop)
{
    int i = start;

    while(i < stop) {
	int pos = st->decl[i].name;
	if (pos > 0) {
	    int len = st->str[pos-1];  // FIXME: RODATA
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
    int start = (st->mdef != BAD_INDEX) ? INDEX(st->mdef)+1 : 0;
    return lookup_decl_in(st, name, name_len, start, st->ps.nd);
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

index_t lookup_string_const(csp_rt_t* st, char* str, int slen)
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

NOINLINE index_t csp_new_decl(csp_rt_t* st, char* name, int name_len, decl_t type)
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

NOINLINE index_t new_signed_const(csp_rt_t* st, ivalue_t v)
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

NOINLINE index_t new_float_const(csp_rt_t* st, fvalue_t v)
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

NOINLINE index_t new_string_const(csp_rt_t* st, char* str, int len)
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

int csp_new_next(csp_rt_t* st, int r)
{
    int i;
    if ((i = csp_new_instr(st, OP_NEXT)) >= 0) {
	st->instr[i].x.x = r;
    }
    return i;
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

int csp_new_liu(csp_rt_t* st, reg_t x, uint16_t imm)
{
    return csp_new_imm(st, OP_LIU, x, (int16_t)imm);
}

int csp_new_lih(csp_rt_t* st, reg_t x, uint16_t imm)
{
    return csp_new_imm(st, OP_LIH, x, (int16_t)imm);
}

// Smart load: choose LI, LIU, or LIU+LIH based on value
int csp_load_int(csp_rt_t* st, reg_t x, ivalue_t val)
{
    if ((val >= -32768) && (val <= 32767)) {
	return csp_new_li(st, x, (int16_t)val);
    }
    else {
	uint32_t uval = (uint32_t)val;
	if (csp_new_liu(st, x, (uint16_t)(uval & 0xFFFF)) < 0)
	    return -1;
	if (uval > 0xFFFF) {
	    if (csp_new_lih(st, x, (uint16_t)(uval >> 16)) < 0)
		return -1;
	}
	return 0;
    }
}

int csp_load_uint(csp_rt_t* st, reg_t x, uvalue_t val)
{
    if (val <= 32767) {
	return csp_new_li(st, x, (int16_t)val);
    }
    else if (val <= 0xFFFF) {
	return csp_new_liu(st, x, (uint16_t)val);
    }
    else {
	if (csp_new_liu(st, x, (uint16_t)(val & 0xFFFF)) < 0)
	    return -1;
	return csp_new_lih(st, x, (uint16_t)(val >> 16));
    }
}

int csp_load_float(csp_rt_t* st, reg_t x, fvalue_t val)
{
#if FVALUE_IS_FIXPOINT
    // Fixpoint is just an int32_t, load as signed
    return csp_load_int(st, x, (ivalue_t)val);
#else
    union { float f; uint32_t u; } v;
    v.f = val;
    if (v.u == 0) {
	return csp_new_li(st, x, 0);  // 0.0
    }
    if (csp_new_liu(st, x, (uint16_t)(v.u & 0xFFFF)) < 0)
	return -1;
    return csp_new_lih(st, x, (uint16_t)(v.u >> 16));
#endif
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
    return csp_new_alu(st, op, x, y, 0);
}

int new_expr0(csp_rt_t* st, opcode_t op)
{
    return csp_new_alu(st, op, 0, 0, 0);
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

// load immedate value.
NOINLINE int csp_load_value(csp_rt_t* st, reg_t x, vtype_t vt, value_t val)
{
    switch(vt) {
    case V_INDEX:
	return csp_new_li(st, x, val.i);
    case V_INTEGER:
	return csp_load_int(st, x, val.i);
    case V_UNSIGNED:
	return csp_load_uint(st, x, val.u);
    case V_FLOAT:
	return csp_load_float(st, x, val.f);
    default:
	return -1;
    }
}

// Add unique variable to var list (for <- parsing)
static void add_var(csp_rt_t* st, index_t ix)
{
    int i;
    for (i = 0; i < st->nvar; i++)
	if (st->var[i] == ix) return;  // already in list
    if (st->nvar < MAX_TIMERS)
	st->var[st->nvar++] = ix;
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
	add_var(st, ix);
	if (csp_new_ld(st,dst,ix) < 0)
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
	I = 1;
	val = csp_value(st, ix);
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
	if (new_expr1(st, OP_CVTIF, r, ent.reg) < 0)
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
	if (new_expr1(st, OP_CVTFI, r, ent.reg) < 0)
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
NOINLINE static int process_assign(csp_rt_t* st, rentry_t* rstack, int ep)
{
    rentry_t lhs = rstack[ep-2];
    rentry_t rhs = rstack[ep-1];
    vtype_t ltype;

#ifdef DEBUG
    printf("ASSIGN ");
    print_rentry(st, "lhs", &lhs);
    print_rentry(st, "rhs", &rhs);    
    printf("\n");
#endif

    if (lhs.ix == BAD_INDEX) {
	csp_set_error(st, ERR_SYNTAX);  // left side must be l-value
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
	csp_set_error(st, ERR_SYNTAX);  // rhs must have value
	return -1;
    }
    
    if (!st->ap) {
	if (rhs.I)
	    csp_set_value(st, lhs.ix, rhs.val);
    }
    else { // Generate store instruction	
	if (csp_new_st(st, rhs.reg, lhs.ix) < 0)
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
    case OP_LT: return OP_FLT;
    case OP_LTE: return OP_FLTE;
    case OP_GT: return OP_FGT;
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
	case EQ:
	    if ((ep = process_assign(st, rstack, ep)) < 0)
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

	    // Type coercion: promote to float if either operand is float
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
	    printf("op=%s\n", csp_opcode_name(op));
	    print_rentry(st, "L", a);
	    print_rentry(st, "R", b);
	    printf("\n");
#endif
	    //
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
			if (new_expr2(st, op, dst, a->reg, b->reg) < 0)
			    return PARSE_ERROR;
			free_reg(st, a->reg);
			if (a->reg != b->reg)
			    free_reg(st, b->reg);
		    }
		    a->reg = dst;
		    a->I = 0;
		    a->vt = rt;
		}
		else
		    return -1;
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
	printf("op=%s\n", csp_opcode_name(op));
	print_rentry(st, "A", a);
	printf("\n");
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
		    if (new_expr1(st, op, dst, a->reg) < 0)
			return PARSE_ERROR;
		    free_reg(st, a->reg);
		}
		a->reg = dst;
		a->I = 0;
		a->vt = rt;
	    }
	    else
		return -1;
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

    if ((func = csp_match_func(st, word->v.str.ptr, word->v.str.len,
			       arity, rarg, &is_user, &func_idx)) == NULL)
	return -1;
    n = arity;
    for (j = 0; j < n; j++) {
	rentry_t arg = rarg[j];
	vtype_t argvt = arg.vt;
	vtype_t argtype = (func->argtypes >> 4*j);
	
	argcode |= (argvt << 4*j);
	argimm  |= (arg.I << j);

	// check arguments & coerce
	switch(argtype) {
	case V_ANY:
	    break;  // OK
	case V_NUMBER:
	    if (!((argvt == V_INTEGER) || (argvt == V_FLOAT)))
		return 0;
	    break;
	case V_INTEGER:
	    if (coerce_to_int(st, &arg) < 0)
		return 0;
	    break;
	case V_FLOAT:
	    if (coerce_to_float(st, &arg) < 0)
		return 0;
	    break;
	case V_STRING:
	    if (argvt != V_STRING)
		return 0;
	    break;
	case V_INDEX:
	    if (!arg.X) { // must be a "variable"
		csp_set_error(st, ERR_SYNTAX);
		return -1;
	    }
	    arg.X = 0;
	    arg.I = 1;
	    arg.val.u = arg.ix;
	    break;
	default:
	    if (argtype != argvt)
		return 0;
	    break;
	}
	if (csp_load(st, &arg) < 0) {
	    csp_set_error(st, ERR_SYNTAX);
	    return -1;
	}
	if (csp_new_arg(st, arg.reg, j) < 0)
	    return -1;
	if (arg.L) free_reg(st, arg.reg);
    }
    // check if we can evaluate a pure function
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
    if (csp_new_call(st, dst, func_idx, is_user, argcode) < 0)
	return 0;
    return push_reg(rstack, ep, dst, func->rtype, dval, imm);
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
	    // Not a function - regular variable/decl lookup
	    if ((ix = lookup_decl(st,tval.str.ptr,tval.str.len)) == BAD_INDEX) {
		csp_set_error(st, ERR_VARIABLE_NOT_DECLARED);
		return 0;
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
		    return 0;
		}
		ix = MAKE_INDEX(st->decl[INDEX(ix)].mq.m,INDEX(jx));
		i += 2;
	    }
	    // Apply module context
	    if ((st->mdef != BAD_INDEX) && (OBJ(ix) == 0))
		ix = MAKE_INDEX(CURRENT, INDEX(ix));

	    // Check if this is an l-value (assignment target)
	    vtype_t vt = st->decl[INDEX(ix)].vt;
	    if ((i < n) && (tv[i].t == EQ)) {
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
	    ostack[pp++] = tok;
	}
	else {
	    tok_t tok2 = ostack[pp-1];
	    int p2;
	    // FUNC_MARKER acts like LP - don't process operators past it
	    if (IS_FUNC_MARKER(tok2) || tok2 == LP) {
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

// expect tokens from tv[] matching pattern in te[].t
// on match, copies token values to te[].v and updates *pi
// returns: 1=match, 0=no match
NOINLINE static int expect(csp_rt_t* st, token_t* tv, int* pi, size_t n, token_t* te)
{
    int i = *pi;
    int j = 0;
    tok_t t, s;
    reg_allocator_t* saved_ap;

    do {
	t = te[j].t;
	if (t == LAST)
	    break;
	if (i >= (int)n) return 0;  // bounds check
	s = tv[i].t;

	if ((t == INT) || (t == FLT)) {
	    // try to parse constant expression
	    rentry_t result;
	    size_t num = 1;  // start with 1 token, parse_expr will consume more if needed

	    // find how many tokens until end of line or next expected token
	    // stop at EQ since it typically starts init value, not part of size expr
	    int k = i;
	    tok_t next = te[j+1].t;
	    while (k < (int)n && tv[k].t != NEWLINE && tv[k].t != NONE &&
		   (tv[k].t != EQ) && (next == LAST || tv[k].t != next))
		k++;
	    num = k - i;
	    if (num == 0) num = 1;
	    saved_ap = st->ap;
	    st->ap = NULL;  // no codegen
	    if (!csp_parse_expr(st, &tv[i], &num, &result)) {
		st->ap = saved_ap;
		return 0;
	    }
	    st->ap = saved_ap;

	    if (!result.I)
		return 0;  // not a constant

	    // check type
	    if ((t == INT) &&
		(result.vt != V_INTEGER) && (result.vt != V_UNSIGNED))
		return 0;
	    if ((t == FLT) && (result.vt != V_FLOAT))
		return 0;

	    te[j].v.val = result.val;
	    i += num;  // skip consumed tokens
	}
	else {
	    // simple token match
	    if (t != s)
		return 0;
	    te[j].v = tv[i].v;  // copy value
	    i++;
	}
	j++;
    } while(1);
    *pi = i;  // update position
    return 1;
}

typedef struct PACKED {
    union {
	unsigned bits:16;
	struct {
	    unsigned dir:DIR_BITS;
	    unsigned endian:ENDIAN_BITS;
	    unsigned vt:TYPE_BITS;
	    unsigned pwm:1;
	    unsigned pullup:1;
	    unsigned pulldown:1;
	};
    };
} decl_opts_t;

NOINLINE static decl_opts_t parse_opts(csp_rt_t* st, token_t* tv,
					int* ip, size_t n)
{
    int i = *ip;
    decl_opts_t opts;

    opts.bits = 0;
opts:
    if (i < (int)n) {
	switch(tv[i].t) {
	case UNSIGNED: opts.vt=V_UNSIGNED; i++; goto opts;
	case INTEGER:  opts.vt=V_INTEGER; i++; goto opts;
	case FLOAT:    opts.vt=V_FLOAT; i++;  goto opts;
	case PWM:      opts.pwm = 1; i++; goto opts;	    
	case IN:       opts.dir |= DIR_IN; i++; goto opts;
	case OUT:      opts.dir |= DIR_OUT; i++; goto opts;
	case INOUT:    opts.dir |= DIR_INOUT; i++; goto opts;
	case LITTLE:   opts.endian=E_LITTLE; i++; goto opts;
	case BIG:      opts.endian=E_BIG; i++; goto opts;
	case PULLUP:   opts.pullup=1; i++; goto opts;
	case PULLDOWN: opts.pulldown=1; i++; goto opts;
	default: break;
	}
    }
    *ip= i;
    return opts;
}

// '#' 'module' <name>
NOINLINE int csp_parse_module(csp_rt_t* st, token_t* tv, size_t n)
{
    index_t ix;
    index_t jx;
    int i = 0;
    token_t te[] = {{HASH}, {MODULE}, {WORD}, {LAST}};

    if (!expect(st, tv, &i, n, te)) {
	csp_set_error(st, ERR_SYNTAX);
	return -1;
    }
    if ((ix = lookup_decl(st, te[2].v.str.ptr, te[2].v.str.len)) != BAD_INDEX) {
	csp_set_error(st, ERR_OBJECT_ALREADY_DEFINED);
	return -1;
    }
    ix = csp_new_decl(st, te[2].v.str.ptr, te[2].v.str.len, DECL_MODULE);
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
NOINLINE int csp_parse_end(csp_rt_t* st, token_t* tv, size_t n)
{
    index_t mx, ex, lx;
    int i = 0;
    token_t te[] = {{HASH}, {END}, {LAST}};

    if (!expect(st, tv, &i, n, te)) {
	csp_set_error(st, ERR_SYNTAX);
	return -1;
    }
    if ((mx = st->mdef) != BAD_INDEX) { // stack?
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
	st->mdef = BAD_INDEX;
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
NOINLINE int csp_parse_variable(csp_rt_t* st, token_t* tv, size_t n)
{
    ivalue_t res = MAKE_RES(8*sizeof(ivalue_t));
    value_t def;
    index_t ix;
    int i = 0;
    token_t te[] = {{HASH}, {VARIABLE}, {WORD}, {LAST}};
    token_t teres[] = {{COLON}, {INT}, {LAST}};
    // pindir_t dir = 0;
    decl_opts_t opts;

    if (!expect(st, tv, &i, n, te)) {
	csp_set_error(st, ERR_SYNTAX);
	return -1;
    }
    if (expect(st, tv, &i, n, teres))
	res = MAKE_RES(teres[1].v.val.i);
    def.u = 0;

    opts = parse_opts(st, tv, &i, n);
    if (opts.vt == 0) opts.vt = V_INTEGER;
    
    if (i < (int)n && tv[i].t == EQ) {
	token_t teeq[] = {{EQ}, {(opts.vt == V_FLOAT) ? FLT : INT}, {LAST}};
	if (!expect(st, tv, &i, n, teeq)) {
	    csp_set_error(st, ERR_SYNTAX);
	    return -1;
	}
	def = teeq[1].v.val;
    }
    if ((ix = lookup_decl(st, te[2].v.str.ptr, te[2].v.str.len)) == BAD_INDEX)
	ix = csp_new_decl(st, te[2].v.str.ptr, te[2].v.str.len, DECL_VARIABLE);
    if (ix == BAD_INDEX) return -1;
    i = INDEX(ix);
    st->decl[i].vt = opts.vt;
    st->decl[i].res = res;
    st->decl[i].dir = opts.dir;
    st->decl[i].va.init = def;
    return 0;
}

// '#' 'constant' <name>[':' <size>] [<opt>+] '=' <num>
NOINLINE int csp_parse_constant(csp_rt_t* st, token_t* tv, size_t n)
{
    ivalue_t res;
    value_t cnst;
    index_t ix;
    int i = 0;
    token_t teres[] = {{COLON}, {INT}, {LAST}};
    token_t te[] = {{HASH}, {CONSTANT}, {WORD}, {LAST}};
    token_t teeq[3] = {{EQ}, {INT}, {LAST}};
    decl_opts_t opts;
    
    cnst.u = 0;
    res = MAKE_RES(8*sizeof(ivalue_t));
    
    if (!expect(st, tv, &i, n, te)) {
	csp_set_error(st, ERR_SYNTAX);
	return -1;
    }
    if (expect(st, tv, &i, n, teres))
	res = MAKE_RES(teres[1].v.val.i);

    opts = parse_opts(st, tv, &i, n);
    if (opts.vt == 0) opts.vt = V_INTEGER;    

    teeq[1].t = (opts.vt == V_FLOAT) ? FLT : INT;
    if (!expect(st, tv, &i, n, teeq)) {
	csp_set_error(st, ERR_SYNTAX);
	return -1;
    }
    cnst = teeq[1].v.val;
    if ((ix = lookup_decl(st, te[2].v.str.ptr, te[2].v.str.len)) == BAD_INDEX)
	ix = csp_new_decl(st, te[2].v.str.ptr, te[2].v.str.len, DECL_CONSTANT);
    if (ix == BAD_INDEX) return -1;
    i = INDEX(ix);
    st->decl[i].res = res;
    st->decl[i].vt = opts.vt;
    st->decl[i].cn.init = cnst;
    return 0;    
}

// '#' 'digital' <name> [<iodir>|<pull>] [<port>':']<pin>
NOINLINE int csp_parse_digital(csp_rt_t* st, token_t* tv, size_t n)
{
    ivalue_t res = MAKE_RES(1);
    ivalue_t port=0, pin=0;
    index_t ix;
    int i = 0;
    token_t te[] = {{HASH}, {DIGITAL}, {WORD}, {LAST}};
    token_t tepin[] = {{INT}, {LAST}};
    token_t teport[] = {{INT}, {COLON}, {INT}, {LAST}};
    decl_opts_t opts;
    
    if (!expect(st, tv, &i, n, te)) {
	csp_set_error(st, ERR_SYNTAX);
	return -1;
    }

    opts = parse_opts(st, tv, &i, n);
    if (opts.dir == 0) opts.dir = DIR_IN;

    if (expect(st, tv, &i, n, teport)) {
	port = teport[0].v.val.i;
	pin  = teport[2].v.val.i;
    }
    else if (expect(st, tv, &i, n, tepin)) {
	pin = tepin[0].v.val.i;
    }
    else {
	csp_set_error(st, ERR_SYNTAX);
	return -1;
    }
    if ((ix = lookup_decl(st, te[2].v.str.ptr, te[2].v.str.len)) == BAD_INDEX)
	ix = csp_new_decl(st, te[2].v.str.ptr, te[2].v.str.len, DECL_DIGITAL);
    if (ix == BAD_INDEX) return -1;
    i = INDEX(ix);
    st->decl[i].res = res;
    st->decl[i].di.pin = pin;
    st->decl[i].di.port = port;
    st->decl[i].dir = opts.dir;
    st->decl[i].di.pullup = opts.pullup;
    st->decl[i].di.pulldown = opts.pulldown;
    return 0;
}

//'#' 'analog' <name> [':'<size>] [<opt>*]  [<port>':'] <pin>
//   <opt> := 'in' | 'out' | 'inout' | 'pwm' | 'float' | 'signed' | 'unsigned'
NOINLINE int csp_parse_analog(csp_rt_t* st, token_t* tv, size_t n)
{
    ivalue_t res;
    ivalue_t port=0, pin=0;
    index_t ix;
    int i = 0;
    token_t te[] = {{HASH}, {ANALOG}, {WORD}, {LAST}};
    token_t teres[] = {{COLON}, {INT}, {LAST}};
    token_t tepin[] = {{INT}, {LAST}};
    token_t teport[] = {{INT}, {COLON}, {INT}, {LAST}};
    decl_opts_t opts;
    
    if (!expect(st, tv, &i, n, te)) {
	csp_set_error(st, ERR_SYNTAX);
	return -1;
    }
    res = MAKE_RES(10);
    if (expect(st, tv, &i, n, teres))
	res = MAKE_RES(teres[1].v.val.i);

    opts = parse_opts(st, tv, &i, n);
    if (opts.vt == 0) opts.vt = V_INTEGER;
    if (opts.dir == 0) opts.dir = DIR_IN;
    
    if (expect(st, tv, &i, n, teport)) {
	port = teport[0].v.val.i;
	pin  = teport[2].v.val.i;
    }
    else if (expect(st, tv, &i, n, tepin)) {
	pin = tepin[0].v.val.i;
    }
    else {
	csp_set_error(st, ERR_SYNTAX);
	return -1;
    }
    
    if ((ix = lookup_decl(st, te[2].v.str.ptr, te[2].v.str.len)) == BAD_INDEX)
	ix = csp_new_decl(st, te[2].v.str.ptr, te[2].v.str.len, DECL_ANALOG);
    if (ix == BAD_INDEX) return -1;
    i = INDEX(ix);
    st->decl[i].vt = opts.vt;
    st->decl[i].res = res;
    st->decl[i].dir = opts.dir;
    st->decl[i].an.pin = pin;
    st->decl[i].an.port = port;
    st->decl[i].an.pwm = opts.pwm;
    return 0;
}

// '#' 'timer' <name> <milliseconds> ['=' '1'|'0']
NOINLINE int csp_parse_timer(csp_rt_t* st, token_t* tv, size_t n)
{
    ivalue_t init = 0;
    index_t ix, px, tx;
    int i = 0;
    token_t te[] = {{HASH}, {TIMER}, {WORD}, {INT}, {LAST}};
    token_t teeq[] = {{EQ}, {INT}, {LAST}};

    if (!expect(st, tv, &i, n, te)) {
	csp_set_error(st, ERR_SYNTAX);
	return -1;
    }
    if (expect(st, tv, &i, n, teeq))
	init = teeq[1].v.val.i;
    if ((px = lookup_const(st, V_INTEGER, te[3].v.val)) == BAD_INDEX)
	px = new_signed_const(st, te[3].v.val.i);
    tx = csp_new_decl(st, NULL, 0, DECL_VARIABLE);
    i = INDEX(tx);
    st->decl[i].vt = V_UNSIGNED;
    st->decl[i].res = MAKE_RES(32);
    st->decl[i].va.init.u = 0;

    if ((ix = lookup_decl(st, te[2].v.str.ptr, te[2].v.str.len)) == BAD_INDEX) {
	if ((ix = csp_new_decl(st, te[2].v.str.ptr, te[2].v.str.len, DECL_TIMER)) == BAD_INDEX)
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
NOINLINE int csp_parse_can(csp_rt_t* st, token_t* tv, size_t n)
{
    ivalue_t res = MAKE_RES(1);
    index_t ix;
    index_t idx;
    int i = 0;
    int bit0, bit1;
    token_t te[] = {{HASH}, {CAN}, {WORD}, {LAST}};
    token_t teres[] = {{COLON}, {INT}, {LAST}};
    token_t tebit[] = {{INT}, {LB}, {INT}, {RB}, {LAST}};
    token_t tebitrange[] = {{INT},{LB},{INT},{DOT},{DOT},{INT},{RB},{LAST}};
    decl_opts_t opts;    

    if (!expect(st, tv, &i, n, te)) {
	csp_set_error(st, ERR_SYNTAX);
	return -1;
    }
    if (expect(st, tv, &i, n, teres))
	res = MAKE_RES(teres[1].v.val.i);

    opts = parse_opts(st, tv, &i, n);
    if (opts.vt == 0) opts.vt = V_INTEGER;
    if (opts.dir == 0) opts.dir = DIR_IN;
	
    // FrameID [bit]
    if (expect(st, tv, &i, n, tebit)) {
	idx = lookup_const(st, V_INTEGER, tebit[0].v.val);
	if (idx == BAD_INDEX)
	    idx = new_signed_const(st, tebit[0].v.val.i);
	bit0 = bit1 = tebit[2].v.val.i;
    }
    // FrameID [bit..bit]
    else if (expect(st, tv, &i, n, tebitrange)) {
	idx = lookup_const(st, V_INTEGER, tebitrange[0].v.val);
	if (idx == BAD_INDEX)
	    idx = new_signed_const(st, tebitrange[0].v.val.i);
	bit0 = tebitrange[2].v.val.i;
	bit1 = tebitrange[5].v.val.i;
    }
    else {
	csp_set_error(st, ERR_SYNTAX);
	return -1;
    }

    if ((ix = lookup_decl(st, te[2].v.str.ptr, te[2].v.str.len)) == BAD_INDEX)
	ix = csp_new_decl(st, te[2].v.str.ptr, te[2].v.str.len, DECL_CAN);
    if (ix == BAD_INDEX) return -1;
    i = INDEX(ix);
    st->decl[i].res = res;
    st->decl[i].vt = opts.vt;
    st->decl[i].dir = opts.dir;
    st->decl[i].ca.id = idx;
    st->decl[i].ca.bit = bit0;
    st->decl[i].ca.len = MAKE_CAN_LEN((bit1-bit0)+1);
    st->decl[i].ca.endian = opts.endian;
    return 0;
}

// '#' word <name>
NOINLINE int csp_parse_object(csp_rt_t* st, token_t* tv, size_t n)
{
    index_t mx, ix, jx;
    int i = 0, m;
    token_t te[] = {{HASH}, {WORD}, {WORD}, {LAST}};

    if (!expect(st, tv, &i, n, te)) {
	csp_set_error(st, ERR_SYNTAX);
	return -1;
    }
    // lookup module
    if ((mx = lookup_decl(st, te[1].v.str.ptr, te[1].v.str.len)) == BAD_INDEX) {
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

    if ((ix = csp_new_decl(st, te[2].v.str.ptr, te[2].v.str.len, DECL_OBJECT)) == BAD_INDEX)
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
NOINLINE static int tok_index(tok_t t, token_t* tv, size_t n)
{
    int i;
    for (i = 0; i < n; i++) {
	if (t == tv[i].t)
	    return i;
    }
    return -1;
}

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

NOINLINE int csp_parse_rule(csp_rt_t* st, token_t* tv, size_t n)
{
    size_t num;
    int i, j, ri;
    int cnd;
    rentry_t result;

    // Check for reactive assignment (<-)
    ri = tok_index(RIMP, tv, n);

    if (ri >= 0) {
	// Reactive assignment: expr1 <- expr2 [? cond]
	// Compiles to: x = body ? cond && (chg(v1)||chg(v2)||...)
	int qi = tok_index(QUEST, tv, n);
	int nn0, body_len, extra_len = 0, cond_len, k;
	rentry_t rhs, extra;
	csp_instr_t temp_extra[16];

	cnd = alloc_reg(st);
	st->nvar = 0;
	nn0 = st->ps.nn;

	// Parse rhs body and collect variables in st->var[]
	num = (qi >= 0 && qi > ri) ? (qi - ri - 1) : (n - ri - 1);
	if (!csp_parse_expr(st, &tv[ri+1], &num, &rhs))
	    return -1;
	body_len = st->ps.nn - nn0;

	if (qi >= 0 && qi > ri) {
	    // Extra condition present: x <- body ? cond
	    int extra_start = st->ps.nn;
	    num = n - (qi + 1);
	    if (!csp_parse_expr(st, &tv[qi+1], &num, &extra))
		return -1;
	    if (!extra.L) csp_load(st, &extra);
	    extra_len = st->ps.nn - extra_start;
	    if (extra_len > 16) return -1;

	    // Save extra to temp, will insert after CHGs
	    memcpy(temp_extra, &st->instr[extra_start],
		   extra_len * sizeof(csp_instr_t));

	    // Layout: LI + CHG*nvar + extra + AND + RULE + body
	    cond_len = 1 + st->nvar + extra_len + 1 + 1;
	    memmove(&st->instr[nn0 + cond_len], &st->instr[nn0],
		    body_len * sizeof(csp_instr_t));
	    memcpy(&st->instr[nn0 + 1 + st->nvar], temp_extra,
		   extra_len * sizeof(csp_instr_t));

	    // Generate LI + CHGs
	    st->ps.nn = nn0;
	    if (csp_new_li(st, cnd, st->nvar ? 0 : -1) < 0) return -1;
	    for (k = 0; k < st->nvar; k++) {
		if (csp_new_mem(st, OP_CHG, cnd, st->var[k]) < 0) return -1;
	    }

	    // Skip past extra (already copied), generate AND
	    st->ps.nn += extra_len;
	    int dst = alloc_reg(st);
	    if (new_expr2(st, OP_AND, dst, extra.reg, cnd) < 0) return -1;
	    free_reg(st, cnd);
	    free_reg(st, extra.reg);
	    cnd = dst;

	    // Generate RULE, then skip body
	    if ((j = csp_new_rule(st, cnd, 0)) < 0) return -1;
	    free_reg(st, cnd);
	    st->ps.nn += body_len;
	} else {
	    // Simple case: x <- body (no extra condition)
	    cond_len = 1 + st->nvar + 1;
	    memmove(&st->instr[nn0 + cond_len], &st->instr[nn0],
		    body_len * sizeof(csp_instr_t));
	    st->ps.nn = nn0;

	    if (csp_new_li(st, cnd, st->nvar ? 0 : -1) < 0) return -1;
	    for (k = 0; k < st->nvar; k++) {
		if (csp_new_mem(st, OP_CHG, cnd, st->var[k]) < 0) return -1;
	    }
	    if ((j = csp_new_rule(st, cnd, 0)) < 0) return -1;
	    free_reg(st, cnd);
	    st->ps.nn += body_len;
	}

	// Parse lhs and generate store
	num = ri;
	if (!csp_parse_expr(st, &tv[0], &num, &result))
	    return -1;
	if (!rhs.L) csp_load(st, &rhs);
	if (result.ix != BAD_INDEX) {
	    if (csp_new_mem(st, OP_STIMP, rhs.reg, result.ix) < 0)
		return -1;
	}

	st->instr[j].r.nxt = st->ps.nn - j;
	if (csp_new_next(st, rhs.reg) < 0)
	    return -1;
	free_reg(st, rhs.reg);
	if (result.L) free_reg(st, result.reg);
	return 0;
    }

    // Standard rule: expr ? cond
    if ((i=tok_index(QUEST, tv, n)) >= 0) {
	// parse condition
	num = n - (i+1);
	if (!csp_parse_expr(st, &tv[i+1], &num, &result))
	    return -1;
	if (!result.L)
	    csp_load(st, &result);
	cnd = result.reg;
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
    free_reg(st, cnd);
    if (!csp_parse_expr(st, &tv[0], &num, &result))
	return -1;
    st->instr[j].r.nxt = st->ps.nn - j;
    if (csp_new_next(st, result.reg) < 0)
	return -1;
    free_reg(st, result.reg);
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
    st->decl[i].dir = DIR_IN;
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
    if (csp_new_ld(st,zr,zx) < 0)
	return -1;

    cnd = alloc_reg(st);
    if (new_expr2(st, OP_EQEQ, cnd, zr, cr) < 0)
	return -1;
    if ((j = csp_new_rule(st, cnd, 0)) < 0)
	return -1;
    if (csp_new_st(st, kr, ox) < 0)
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
    int i = 0;
    token_t te[] = {{INT}, {INT}, {INT}, {INT}, {INT}, {LAST}};

    if (!expect(st, tv, &i, n, te))
	return -1;
    pos = te[1].v.val.i;
    mask = te[2].v.val.i;
    on_bits = te[3].v.val.i;
    off_bits = te[4].v.val.i;

    if ((out = lookup_decl(st, "OUT", 3)) == BAD_INDEX) {
	// fixme: pin number etc for standard OUT
	if ((out = csp_new_decl(st,"OUT",3,DECL_DIGITAL)) == BAD_INDEX)
	    return -1;
    }

    if ((idx = lookup_const(st, V_INTEGER, te[0].v.val)) == BAD_INDEX)
	idx = new_signed_const(st, te[0].v.val.i);    
    

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
    
NOINLINE int csp_parse(csp_rt_t* st, char* str)
{
    token_t tv[MAX_LINE_TOKENS];
    size_t num = MAX_LINE_TOKENS;
    reg_allocator_t alloc;
    int n;
    token_t te[] = {{INT}, {INT}, {INT}, {INT}, {INT}, {LAST}};

    st->ap = &alloc;

    while((n = csp_scan_line(str, tv, &num)) > 0) {
	int r = -1;
	int i = 0;  // for expect peek
	str += n;
	alloc_init(st->ap);

	if (tv[0].t == NEWLINE)
	    r = 0;
	else if (tv[0].t == INT && expect(st, tv, &i, num, te)) {
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
    st->cur = 0;      // current module = global
    st->mdef = BAD_INDEX;  // no module being defined
    st->var = st->timer;  // reuse timer[] for var list during <- parse
    st->nvar = 0;

    st->str[0] = 0;  // reserved 0 and nil
    st->ufuncs = NULL;
    st->num_ufuncs = 0;
    st->uconst = NULL;
    // new_signed_const(st, 0);
    // new_signed_const(st, 1);
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
	    if (st->decl[i].dir & DIR_IN) {
		if (st->ni < MAX_INPUTS) // warning?
		    st->input[st->ni++] = ix;
	    }
	    if (st->decl[i].dir & DIR_OUT) {
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
