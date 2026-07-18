// CandySpeak runtime
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "csp.h"
#include "csp_strings.h"   // shared RODATA strings (generated from strings.tab)
#include "csp_parse.h"
#include "csp_print.h"
#include "bitpack.h"
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
#define ISBLANK(c) (((c) == ' ') || ((c) == '\t'))
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

const op_entry_t decl_table[] RODATA = {
    DECL_ENT(D_NONE,DECL_NONE,s_none),
    DECL_ENT(D_MODULE,DECL_MODULE,s_module),
    DECL_ENT(D_END,DECL_END, s_end),
    DECL_ENT(D_STATES,DECL_STATES,s_states),
    DECL_ENT(D_IN,DECL_IN,s_in),       // 'in' FIXME? used as option keyword!
    DECL_ENT(D_CONSTANT,DECL_CONSTANT,s_constant),
    DECL_ENT(D_VARIABLE,DECL_VARIABLE,s_variable),
    DECL_ENT(D_DIGITAL,DECL_DIGITAL,s_digital),
    DECL_ENT(D_ANALOG,DECL_ANALOG,s_analog),
    DECL_ENT(D_TIMER,DECL_TIMER,s_timer),
    DECL_ENT(D_CAN,DECL_CAN,s_can),
    DECL_ENT(D_BUFFER,DECL_BUFFER,s_buffer),
    DECL_ENT(D_LAST,DECL_NONE,s_null),
};

const op_entry_t tok_table[] RODATA = {
    INSTR_ENT(NONE,OP_NOP,s_NOP,-1,0,NO),
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
    // EQ/RIMP are shunted as low-precedence right-assoc operators so that the
    // expression parser handles `var = expr` -- needed for immediate `> T1=1`
    // (one-shot assignment / timer start). Rules use asm_rule, not this path.
    INSTR_ENT(EQ,OP_EQ,s_EQ,2,5,RIGHT),       // assign_expr
    INSTR_ENT(RIMP,OP_RIMP,s_RIMP,2,4,RIGHT), // assign_expr
    INSTR_ENT(COMMA,OP_COMMA,s_COMMA,2,2,RIGHT),
    INSTR_ENT(QUEST,OP_RULE,s_QUEST,-1,-1,NO),

    TOK_ENT(PULLUP,OP_NOP,s_pullup),
    TOK_ENT(PULLDOWN,OP_NOP,s_pulldown),
    TOK_ENT(RESOLUTION,OP_NOP,s_resolution),
    TOK_ENT(IN,OP_NOP,s_in),
    TOK_ENT(OUT,OP_NOP,s_out),
    TOK_ENT(INOUT,OP_NOP,s_inout),
    TOK_ENT(T_PWM,OP_NOP,s_pwm),
    TOK_ENT(FLOAT,OP_NOP,s_float),
    TOK_ENT(INTEGER,OP_NOP,s_integer),
    TOK_ENT(UNSIGNED,OP_NOP,s_unsigned),
    TOK_ENT(STRING,OP_NOP,s_string),
    TOK_ENT(NATIVE,OP_NOP,s_native),    
    TOK_ENT(LITTLE,OP_NOP,s_little),
    TOK_ENT(BIG,OP_NOP,s_big),
    TOK_ENT(T_CAN,OP_NOP,s_can),   // shares the #can keyword's string

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
    TOK_ENT(T_LAST,OP_NOP,s_null)
};

// Function calls are stored in ostack as (LAST + 1 + func_index)
// func_index encodes: (index << 1) | is_user
// Note: must use LAST (not LAST_NODE) to avoid overlap with LP, RP, etc.
// Fixme: (fname-token-index:8,ostack-depth:8,last:8)
// make ostack uint32_t
#define FUNC_MARKER_BASE (T_LAST + 1)
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

// opcode => opcode type info
const op_info_t op_info[] RODATA = {
    [OP_ADD] = {s_ADD,PLUS,2,V_INTEGER,MAKE_TYPE2(V_INTEGER,V_INTEGER)},
    [OP_SUB] = {s_SUB,MINUS,2,V_INTEGER,MAKE_TYPE2(V_INTEGER,V_INTEGER)},
    [OP_MUL] = {s_MUL,ASTERISK,2,V_INTEGER,MAKE_TYPE2(V_INTEGER,V_INTEGER)},
    [OP_DIV] = {s_DIV,SLASH,2,V_INTEGER,MAKE_TYPE2(V_INTEGER,V_INTEGER)},
    [OP_REM] = {s_REM,PERCENT,2,V_INTEGER,MAKE_TYPE2(V_INTEGER,V_INTEGER)},
    [OP_SLA] = {s_SLA,LTLT,2,V_INTEGER,MAKE_TYPE2(V_INTEGER,V_INTEGER)},
    [OP_SRA] = {s_SRA,GTGT,2,V_INTEGER,MAKE_TYPE2(V_INTEGER,V_INTEGER)},
    [OP_BAND] = {s_BAND,AMP,2,V_INTEGER,MAKE_TYPE2(V_INTEGER,V_INTEGER)},
    [OP_BOR] = {s_BOR,BAR,2,V_INTEGER,MAKE_TYPE2(V_INTEGER,V_INTEGER)},
    [OP_BXOR] = {s_BXOR,CIRC,2,V_INTEGER,MAKE_TYPE2(V_INTEGER,V_INTEGER)},
    [OP_AND] = {s_AND,AMPAMP,2,V_INTEGER,MAKE_TYPE2(V_INTEGER,V_INTEGER)},
    [OP_OR] = {s_OR,BARBAR,2,V_INTEGER,MAKE_TYPE2(V_INTEGER,V_INTEGER)},
    [OP_EQ] = {s_ASSIGN,EQ,2,V_INTEGER,MAKE_TYPE2(V_INTEGER,V_INTEGER)},
    [OP_LT] = {s_OLT,LT,2,V_INTEGER,MAKE_TYPE2(V_INTEGER,V_INTEGER)},
    [OP_LTE] = {s_OLTE,LTEQ,2,V_INTEGER,MAKE_TYPE2(V_INTEGER,V_INTEGER)},
    [OP_GT] = {s_OGT,GT,2,V_INTEGER,MAKE_TYPE2(V_INTEGER,V_INTEGER)},
    [OP_GTE] = {s_OGTE,GTEQ,2,V_INTEGER,MAKE_TYPE2(V_INTEGER,V_INTEGER)},
    [OP_EQEQ] = {s_OEQEQ,EQEQ,2,V_INTEGER,MAKE_TYPE2(V_INTEGER,V_INTEGER)},
    [OP_NEQ] = {s_ONEQ,NEQ,2,V_INTEGER,MAKE_TYPE2(V_INTEGER,V_INTEGER)},

    // unary versions (treated as binary with z ignored)
    [OP_BNOT] = {s_BNOT,TILDE,1,V_INTEGER,MAKE_TYPE1(V_INTEGER)},
    [OP_NEG] = {s_NEG,MINUS1,1,V_INTEGER,MAKE_TYPE1(V_INTEGER)},
    [OP_MOV] = {s_OMOV,PLUS1,1,V_INTEGER,MAKE_TYPE1(V_INTEGER)},
    [OP_NOT] = {s_NOT,EXCLAMATION,1,V_INTEGER,MAKE_TYPE1(V_INTEGER)},
    [OP_CVTIF] = {s_CVTIF,NONE,1,V_FLOAT,MAKE_TYPE1(V_INTEGER)},   // int→float
    [OP_CVTFI] = {s_CVTFI,NONE,1,V_INTEGER,MAKE_TYPE1(V_FLOAT)},   // float→int

    [OP_FNEG] = {s_FNEG,MINUS1,1,V_FLOAT,MAKE_TYPE1(V_FLOAT)},
    [OP_FMOV] = {s_FMOV,PLUS1,1,V_FLOAT,MAKE_TYPE1(V_FLOAT)},
    [OP_FADD] = {s_FADD,PLUS,2,V_FLOAT,MAKE_TYPE2(V_FLOAT,V_FLOAT)},
    [OP_FSUB] = {s_FSUB,MINUS,2,V_FLOAT,MAKE_TYPE2(V_FLOAT,V_FLOAT)},
    [OP_FMUL] = {s_FMUL,ASTERISK,2,V_FLOAT,MAKE_TYPE2(V_FLOAT,V_FLOAT)},
    [OP_FDIV] = {s_FDIV,SLASH,2,V_FLOAT,MAKE_TYPE2(V_FLOAT,V_FLOAT)},

    [OP_FLT] = {s_FLT,LT,2,V_INTEGER,MAKE_TYPE2(V_FLOAT,V_FLOAT)},
    [OP_FLTE] = {s_FLTE,LTEQ,2,V_INTEGER,MAKE_TYPE2(V_FLOAT,V_FLOAT)},
    [OP_FGT] = {s_FGT,GT,2,V_INTEGER,MAKE_TYPE2(V_FLOAT,V_FLOAT)},
    [OP_FGTE] = {s_FGTE,GTEQ,2,V_INTEGER,MAKE_TYPE2(V_FLOAT,V_FLOAT)},
    [OP_FEQEQ] = {s_FEQ,EQEQ,2,V_INTEGER,MAKE_TYPE2(V_FLOAT,V_FLOAT)},
    [OP_FNEQ] = {s_FNEQ,NEQ,2,V_INTEGER,MAKE_TYPE2(V_FLOAT,V_FLOAT)},

    // comman may not be needed?
    [OP_COMMA] = {s_OCOMMA,NONE,2,V_INTEGER,MAKE_TYPE2(V_INTEGER,V_INTEGER)},

    // other operations for name
    [OP_ENTER] = {s_ENTER,NONE,-1,V_VOID,MAKE_TYPE0()},
    [OP_LEAVE] = {s_LEAVE,NONE,-1,V_VOID,MAKE_TYPE0()},
    [OP_NEW]   = {s_NEW,NONE,-1,V_VOID,MAKE_TYPE0()},
    [OP_LI]    = {s_LI,NONE,-1,V_VOID,MAKE_TYPE0()},
    [OP_LIU]   = {s_LIU,NONE,-1,V_VOID,MAKE_TYPE0()},
    [OP_LIH]   = {s_LIH,NONE,-1,V_VOID,MAKE_TYPE0()},
    [OP_ARG]   = {s_ARG,NONE,-1,V_VOID,MAKE_TYPE0()},
    [OP_ST]    = {s_ST,NONE,-1,V_VOID,MAKE_TYPE0()},
    [OP_STP]   = {s_STP,NONE,-1,V_VOID,MAKE_TYPE0()},
    [OP_STIMP] = {s_STIMP,NONE,-1,V_VOID,MAKE_TYPE0()},
    [OP_CHG]   = {s_CHG,NONE,-1,V_VOID,MAKE_TYPE0()},
    [OP_EQI]   = {s_EQI,NONE,-1,V_VOID,MAKE_TYPE0()},
    [OP_STI]   = {s_STI,NONE,-1,V_VOID,MAKE_TYPE0()},
    [OP_INSTATE] = {s_INSTATE,NONE,-1,V_VOID,MAKE_TYPE0()},
    [OP_LD]    = {s_LD,NONE,-1,V_VOID,MAKE_TYPE0()},
    [OP_LDP]   = {s_LDP,NONE,-1,V_VOID,MAKE_TYPE0()},
    [OP_CALL]  = {s_CALL,NONE,-1,V_VOID,MAKE_TYPE0()},
    [OP_RULE]  = {s_RULE,NONE,-1,V_VOID,MAKE_TYPE0()},
    [OP_NEXT]  = {s_NEXT,NONE,-1,V_VOID,MAKE_TYPE0()},
    [OP_NOP]   = {s_NOP,NONE,-1,V_VOID,MAKE_TYPE0()},
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

// The firmware ROM image lives in rom.c, which every build links exactly once
// (an empty default rom.c provides zero-sized stubs when no program is baked
// in). These are plain externs -- NOT weak fallbacks defined here: a weak
// definition in this TU would be bound locally by csp_load_rom and let
// -fdata-sections/--gc-sections drop the strong rom.c copy, so the firmware
// would boot with an empty ROM. rom_n_edg > 0 means the ROM carries its own
// precomputed reactive graph (ROM decl -> ROM rules), consumed by
// csp_enq_elist alongside the runtime RAM graph. Emitted by csp -C -r.
extern const char        rom_str[];
extern const int         rom_str_len;
extern const csp_decl_t  rom_decl[];
extern const csp_instr_t rom_instr[];
extern const int         rom_n_decl;
extern const int         rom_n_instr;
extern const int         rom_n_edg;
extern const index_t     rom_idg[];
extern const index_t     rom_ofs[];
extern const index_t     rom_edg[];
// State table (name<->number) baked with the program: rule listing and runtime
// state lookup need the user states, which csp_rt_init only seeds with
// INIT/NORMAL. Always emitted (rom_n_states>=2 for a baked program, 0 stub when
// no firmware is linked).
extern const int         rom_n_states;
extern const state_t     rom_states[];

// Segment-aware reads (see csp.h). NOINLINE keeps the flash-copy in one place
// instead of expanding it at every decl()/instr() call site.
NOINLINE csp_decl_t csp_get_decl(csp_rt_t* st, index_t i)
{
    if (i < st->rom_nd)
	return ro_decl(&st->rom_decl_p[i]);
    return st->ram_decl[st->rom_nd - i];   // RAM decls grow down (see ram_decl_at)
}

NOINLINE csp_instr_t csp_get_instr(csp_rt_t* st, index_t n)
{
    if (n < st->rom_nn)
	return ro_instr(&st->rom_instr_p[n]);
    return st->ram_instr[n - st->rom_nn];
}

const char csp_tag(csp_rt_t* st, index_t n)
{
    return tag_tab[decl(st,INDEX(n),type)];
}

static rochar* const pindir_tab[] RODATA = {
    [DIR_NONE] = s_none,
    [DIR_IN]   = s_in,
    [DIR_OUT]  = s_out,
    [DIR_INOUT]  = s_inout
};

rochar* csp_fmt_pindir(uint8_t dir)
{
    return ro_ptr(&pindir_tab[dir&0x3]);
}

rochar* csp_fmt_pull(csp_rt_t* st, int ix)
{
    if (decl(st,ix,di.pullup))
	return s_pullup;
    else if (decl(st,ix,di.pulldown))
	return s_pulldown;
    else
	return s_undefined;  // floating
}

rochar* csp_fmt_pwm(csp_rt_t* st, int ix)
{
    if (decl(st,ix,an.pwm))
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
    return (rochar*) ro_ptr(&vtype_tab[vt & 0xf]);
}

static const char* const endian_tab[] RODATA = {
    [E_NATIVE] = s_native,
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
    case DECL_BUFFER:   RETURN_TSTR(s_buffer);
    default: RETURN_TSTR(s_undefined);
    }
}

#define ify(x) #x
#define stringify(x) ify(x)

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
    case ERR_TOO_MANY_STATES:
	return "too many states";
    case ERR_STATE_NOT_DECLARED:
	return "state %s not declared";
    case ERR_NOT_A_MODULE:
	return "word %s not a module";
    case ERR_END_MISMATCH:
	return "end mismatch";	
    case ERR_OBJECT_NOT_DECLARED:
	return "object %s is not declared";
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
    case ERR_NAME_TOO_LONG:
	return "identifier name to long %d max=" stringify(MAX_NAME_LEN);
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
    return ro_byte(&op_info[op].rtype);
}

uint8_t csp_opcode_arity(opcode_t op)
{
    return ro_byte(&op_info[op].arity);
}

const rochar* csp_opcode_name(opcode_t op)
{
    return (rochar*) ro_ptr(&op_info[op].name);
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
	memcpy(&st->ram_str[st->ps.err_strp], str->ptr, str->len);
	st->ram_str[st->ps.err_strp + str->len] = '\0';
	st->ps.err_args[i] = (uintptr_t)&st->ram_str[st->ps.err_strp];
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

// pointer to a VIEW_SLOT's value_t struct inside its buffer (in the heap)
static inline value_t* csp_slot(csp_rt_t* st, csp_view_t* v, dio_t dir)
{
    return (value_t*)(st->heap[dir] + st->buf[v->buf].hp);
}

// return pointer to the object/field value slot (VIEW_SLOT only)
value_t* csp_dio_slot(csp_rt_t* st, index_t ix, dio_t dir)
{
    return csp_slot(st, csp_view(st, ix), dir);
}

// return pointer to value pointer for input and output (VIEW_SLOT only)
int csp_dio_slots(csp_rt_t* st, index_t ix, value_t** iptr, value_t** optr)
{
    csp_view_t* v = csp_view(st, ix);
    *iptr = csp_slot(st, v, DIN);
    *optr = csp_slot(st, v, DOUT);
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

int csp_print_uintw(uvalue_t v, int nw)
{
    int n;
    while (v < nw) {
	csp_print_char('0');
	nw /= 10;
	n++;
    }
    return n+csp_print_uint(v);
}

#if FVALUE_IS_FIXPOINT
int csp_print_fixpoint(fvalue_t v)
{
    // Print Q16.16 as decimal
    int n;
    int neg = (v < 0);
    uint32_t absv = neg ? -v : v;
    int32_t intpart = absv >> FIX_SHIFT;
    uint32_t fracpart = absv & FIX_MASK;
    // Use 64-bit to avoid overflow: fracpart * 1000000 can exceed 32 bits
    fracpart = (uint32_t)(((uint64_t)fracpart * 1000000) >> FIX_SHIFT);
    if (neg) {
	csp_print_char('-');
	n = 1 + csp_print_uint(intpart);
    }
    else {
	n = csp_print_uint(intpart);
    }
    csp_print_char('.'); n++;
    return n+csp_print_uintw(fracpart, 100000);    
}
#endif

int csp_print_value(csp_rt_t* st, vtype_t vt, value_t val)
{
    switch(vt) {
    case V_INTEGER: return csp_print_int(val.i);
    case V_UNSIGNED: return csp_print_uint(val.u);
    case V_FLOAT: return csp_print_float(val.f);
    case V_STRING: csp_print_str_at(st, val.s); return 1;
    case V_TIMER: return csp_print_int(val.t.val);
    default: return csp_print_str("???");
    }
}

#ifdef DEBUG
void print_rentry(csp_rt_t* st, char* name, rentry_t* rp)
{
    DBG("%s={", name);
    if (rp->X) DBG("name=%s,", decl_name(st, rp->ix));
    DBG("flags=");
    if (rp->I) DBG("im ");
    if (rp->L) DBG("ld ");
    if (rp->X) DBG("ix ");
    DBG(",vt=%s", csp_fmt_vtype(rp->vt));
    if (rp->L) DBG(",reg=%d", rp->reg);
    if (rp->X) DBG(",ix=0x%04x", rp->ix);
    if (rp->I) { DBG(",val="); csp_print_value(st, rp->vt, rp->val); }
    DBG("}");
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
    index_t ix = args[0].u; // timer
    index_t tx = ix+1;
    value_t* vptr = csp_dio_slot(st, ix, DIN);
    if (vptr->t.running)
	ret.u = csp_time_ms() - csp_uvalue(st, tx);
    else
	ret.u = vptr->t.period;
    return ret;
}

//  FIXME: if not running?
static value_t fn_progress(csp_rt_t* st,uint16_t type,
			   value_t* args, uint8_t nargs)
{
    value_t ret;
    index_t ix = args[0].u; // timer
    value_t* vptr = csp_dio_slot(st, ix, DIN);

    if (vptr->t.running) {
	index_t tx = ix+1;      // start time
	uint32_t td = csp_time_ms() - csp_uvalue(st, tx);
	uint32_t period = vptr->t.period;
	ret.f = op_FDIV(op_CVTIF(td), op_CVTIF(period));
    }
    else {
	ret.f = op_CVTIF(1);
    }
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
    int i;
    ret.i = 0;
    for (i = 0; i < nargs; i++) {
	ret.i += csp_print_value(st, type & 0xf, args[i]);
	type >>= 4;
    }
    return ret;
}

static value_t fn_println(csp_rt_t* st, uint16_t type,
			  value_t* args,uint8_t nargs)
{
    value_t ret;    
    ret = fn_print(st, type, args, nargs);
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


#define CSP_FUNC_ENT(str, a, p, rt, args, f)	\
    {.name=(str),.namelen=sizeof((str))-1,.arity=(a),			\
	    .flags=((p)?FUNC_PURE:0)|FUNC_RONAME,			\
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

    // print 1..4 arguments
    CSP_FUNC_ENT(s_print,   1, 0, V_INTEGER, MAKE_TYPE1(V_ANY),  fn_print),
    CSP_FUNC_ENT(s_print,   2, 0, V_INTEGER, MAKE_TYPE2(V_ANY,V_ANY),  fn_print),
    CSP_FUNC_ENT(s_print,   3, 0, V_INTEGER, MAKE_TYPE3(V_ANY,V_ANY,V_ANY),  fn_print),
    CSP_FUNC_ENT(s_print,   4, 0, V_INTEGER, MAKE_TYPE4(V_ANY,V_ANY,V_ANY,V_ANY),  fn_print),
    // println 0..4 arguments    
    CSP_FUNC_ENT(s_println, 0, 0, V_INTEGER, MAKE_TYPE0(),       fn_println),
    CSP_FUNC_ENT(s_println, 1, 0, V_INTEGER, MAKE_TYPE1(V_ANY),  fn_println),
    CSP_FUNC_ENT(s_println, 2, 0, V_INTEGER, MAKE_TYPE2(V_ANY,V_ANY),  fn_println),
    CSP_FUNC_ENT(s_println, 3, 0, V_INTEGER, MAKE_TYPE3(V_ANY,V_ANY,V_ANY),  fn_println),
    CSP_FUNC_ENT(s_println, 4, 0, V_INTEGER, MAKE_TYPE4(V_ANY,V_ANY,V_ANY,V_ANY),  fn_println),
    CSP_FUNC_ENT(s_tick,    0, 0, V_INTEGER, MAKE_TYPE0(),       fn_tick),
    CSP_FUNC_ENT(s_cycle,   0, 0, V_INTEGER, MAKE_TYPE0(),       fn_cycle),
    CSP_FUNC_ENT(s_latch,   1, 0, V_INTEGER, MAKE_TYPE1(V_INTEGER), fn_latch),
};

const uint8_t csp_num_builtin_funcs = sizeof(csp_builtin_funcs)/sizeof(csp_builtin_funcs[0]);

// rom-aware scalar reads: on the host both branches are identical (ro_*==plain);
// on AVR the rom branch uses PROGMEM. One code path serves RAM and ROM tables.
static inline uint8_t  rd8 (const void* p, int rom)
{
    return rom ? ro_byte((const uint8_t*)p): *(const uint8_t*)p;
}

static inline uint16_t rd16(const void* p, int rom)
{
    return rom ? ro_word((const uint16_t*)p): *(const uint16_t*)p;
}

static inline void* rdvp(const void* p, int rom)
{
    void* v;
#if defined(__AVR__)
    if (rom)
	return ro_ptr((void* const*)p);   // PROGMEM: byte-wise read
#endif
    // fn sits at a misaligned offset in the PACKED csp_func_t table; a direct
    // deref HardFaults on Cortex-M0 (no unaligned access). memcpy is byte-wise
    // and alignment-safe -- and the compiler folds it to a plain load where the
    // address happens to be aligned.
    memcpy((void*) &v, p, sizeof(v)); return v;
}

static uint8_t func_arity(const csp_func_t* fn, int i, int rom)
{
    return rd8(&fn[i].arity, rom);
}

// function flags (FUNC_PURE | FUNC_RONAME)
static uint8_t func_flags(const csp_func_t* fn, int i, int rom)
{
    return rd8(&fn[i].flags, rom);
}
#define func_pure(fn,i,rom)   (func_flags((fn),(i),(rom)) & FUNC_PURE)
#define func_roname(fn,i,rom) (func_flags((fn),(i),(rom)) & FUNC_RONAME)

static uint8_t func_namelen(const csp_func_t* fn,int i, int rom)
{
    return rd8(&fn[i].namelen, rom);
}

static const char* func_name(const csp_func_t* fn, int i, int rom)
{
    return (const char*) rdvp(&fn[i].name, rom);
}

static csp_func_fn func_fn(const csp_func_t* fn, int i, int rom)
{
    return (csp_func_fn) rdvp(&fn[i].fn, rom);
}

static uint8_t fn_type(const csp_func_t* fn, int j, int rom)
{
    uint16_t argtypes = rd16(&fn->argtypes, rom);
    return (argtypes >> 4*j) & 0xf;
}

// match function template this code assumes type coerce int->flt
// flt->int. the goal is to match BEST? function to use
// return 0 on match
// return argument number 1...n on mismatch
int csp_match_args(csp_rt_t* st, const csp_func_t* fn, int arity, rentry_t* rarg,
		   int rom)
{
    int j;
    for (j = 0; j < arity; j++) {
	rentry_t arg = rarg[j];
	vtype_t argvt = arg.vt;
	uint8_t ftype = fn_type(fn, j, rom);
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
	    if (arg.X && (decl(st,INDEX(arg.ix),type) == DECL_TIMER)) break;
	    goto mismatch;
	case V_DIGITAL:
	    if (arg.X && (decl(st,INDEX(arg.ix),type) == DECL_DIGITAL)) break;
	    goto mismatch;
	case V_ANALOG:
	    if (arg.X && (decl(st,INDEX(arg.ix),type) == DECL_ANALOG)) break;
	    goto mismatch;
	case V_CAN:
	    if (arg.X && (decl(st,INDEX(arg.ix),type) == DECL_CAN)) break;
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
			const csp_func_t* fn, int num, int rom,
			const tstr_t* name,
			uint8_t arity, rentry_t* rarg)
{
    int i;
    int a, f = -1;
    for (i = 0; i < num; i++) {
	if ((func_arity(fn,i,rom) == arity) &&
	    (func_namelen(fn,i,rom) == name->len)) {
	    const char* fnm = func_name(fn, i, rom);
	    int eq = func_roname(fn,i,rom)
		? (ro_memcmp(name->ptr, fnm, name->len) == 0)   // name in ROM
		: (memcmp(name->ptr, fnm, name->len) == 0);     // name in RAM
	    if (eq) {
		int j;
		if ((j=csp_match_args(st, &fn[i], arity, rarg, rom)) == 0) // ok
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
	if ((idx = csp_match_fn(st, st->ufuncs, st->num_ufuncs, st->ufuncs_rom,
				name, arity, rarg)) >= 0) {
	    *is_user = 1;
	    *func_idx = idx;
	    return &st->ufuncs[idx];
	}
    }
    if ((idx = csp_match_fn(st, csp_builtin_funcs, csp_num_builtin_funcs, BUILTIN_ROM,
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

static int find_op_entry(const op_entry_t* tab, int size,
			 const char* name, int namelen)
{
    int i;
    for (i = 1; i < size; i++) { // assume none entry in slot 0
	uint8_t ronamelen = ro_byte(&tab[i].namelen);
	if (ronamelen == namelen) {
	    const char* roname = ro_ptr(&tab[i].name);
	    if (ro_memcmp(name, roname, ronamelen) == 0)
		return i;
	}
    }
    return -1;
}

static int find_tok_entry(const char* name, int namelen)
{
    return find_op_entry(tok_table, sizeof(tok_table)/sizeof(tok_table[0]),
			 name, namelen);
}

static int find_decl_entry(const char* name, int namelen)
{
    return find_op_entry(decl_table, sizeof(decl_table)/sizeof(decl_table[0]),
			 name, namelen);
}

static inline int8_t op_table_tok(int i)
{
    return ro_byte(&tok_table[i].tok);
}

static inline int8_t op_table_arity(int i)
{
    return ro_byte(&tok_table[i].arity);
}

static inline int8_t op_table_code(int i)
{
    return ro_byte(&tok_table[i].code);
}

static inline int8_t op_table_prec(int i)
{
    return ro_byte(&tok_table[i].prec);
}

static inline int8_t op_table_assoc(int i)
{
    return ro_byte(&tok_table[i].assoc);
}


// enq all rules that depend on declaration x
NOINLINE void csp_enq_elist(csp_rt_t* st, index_t x)
{
#if defined(SUPPORT_REACTIVE) && (SUPPORT_REACTIVE==1)
    int i;
    index_t ix = INDEX(x);
    uint8_t obj = OBJ(x);
    // A CURRENT-relative write (a module rule touching its own object's field)
    // must enqueue under the concrete object, not the CURRENT placeholder, or
    // the dispatcher can't restore the right instance context.
    if (obj == CURRENT)
	obj = st->cur;
    // baked ROM graph in flash: ROM decl -> ROM rules that read it
    if (rom_n_edg && (ix < st->rom_nd)) {
	index_t base = ro_word(&rom_ofs[ix]);
	index_t n    = ro_word(&rom_idg[ix]);
	for (i = 0; i < (int)n; i++)
	    csp_enq(st, obj, ro_word(&rom_edg[base+i]));
    }
    // runtime RAM graph: any decl -> RAM rules that read it. Only decls the graph
    // was built for have edges; one added since (ix >= graph_n) has none yet.
    if (ix < st->graph_n) {
	index_t base = st->ofs[ix];
	for (i = 0; i < st->idg[ix]; i++)
	    csp_enq(st, obj, st->edg[base+i]);  // rule instruction index
    }
#endif
}

// --- buffer heap access (VIEW_HEAP) ----------------------------------------
// Dormant in step 2 (no HEAP views are emitted yet); exercised from step 3.

NOINLINE static value_t csp_heap_get(csp_rt_t* st, csp_view_t* vw, dio_t dir)
{
    csp_buf_t* b = &st->buf[vw->buf];
    uint8_t* p = st->heap[dir] + b->hp;
    value_t v;
    v.u = 0;
    if (vw->flags & VIEW_F_SIMPLE) {       // whole buffer, byte aligned
	uint8_t n = b->nbytes;
	if (n > sizeof(value_t)) n = sizeof(value_t);
	memcpy(&v, p, n);
    }
    else if (vw->endian == E_BIG)
	get_bits_be(p, &v.u, vw->pos, vw->len + 1);
    else
	get_bits_le(p, &v.u, vw->pos, vw->len + 1);
    return v;
}

NOINLINE static void csp_heap_set(csp_rt_t* st, csp_view_t* vw, dio_t dir,
				  value_t v)
{
    csp_buf_t* b = &st->buf[vw->buf];
    uint8_t* p = st->heap[dir] + b->hp;
    if (vw->flags & VIEW_F_SIMPLE) {       // whole buffer, byte aligned
	uint8_t n = b->nbytes;
	if (n > sizeof(value_t)) n = sizeof(value_t);
	memcpy(p, &v, n);
    }
    else if (vw->endian == E_BIG)
	set_bits_be(p, v.u, vw->pos, vw->len + 1);
    else
	set_bits_le(p, v.u, vw->pos, vw->len + 1);
}

// A digital/analog/timer decl carries vt=V_INTEGER (its value type); the
// union member for its config lives under the decl *type*. Map type -> the
// vtype the pin/port/dir/... helpers switch on. Plain vars keep their vt.
NOINLINE static vtype_t decl_cfg_vt(decl_t dt, vtype_t vt)
{
    switch (dt) {
    case DECL_DIGITAL: return V_DIGITAL;
    case DECL_ANALOG:  return V_ANALOG;
    case DECL_TIMER:   return V_TIMER;
    case DECL_CAN:     return V_CAN;
    default:           return vt;
    }
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

NOINLINE void csp_dio_get_pin_part(csp_rt_t* st, value_t* vslot,
				   vtype_t vt, value_t* vp)
{
    switch(vt) {
    case V_DIGITAL: vp->i = vslot->d.pin; break;
    case V_ANALOG:  vp->i = vslot->a.pin; break;
    default: vp->i = 0; break;
    }
}

NOINLINE void csp_dio_get_port_part(csp_rt_t* st, value_t* vslot,
				    vtype_t vt, value_t* vp)
{
    switch(vt) {
    case V_DIGITAL: vp->i = vslot->d.port; break;
    case V_ANALOG:  vp->i = vslot->a.port; break;
    default: vp->i = 0; break;
    }
}

NOINLINE void csp_dio_get_dir_part(csp_rt_t* st, value_t* vslot,
				   vtype_t vt, value_t* vp)
{
    switch(vt) {
    case V_DIGITAL: vp->i = vslot->d.dir; break;
    case V_ANALOG:  vp->i = vslot->a.dir; break;
    default: vp->i = 0; break;
    }
}

// Set value part in dio (config data & value)
NOINLINE void csp_dio_set_part(csp_rt_t* st, index_t ix, value_t v,
			       csp_part_t part, dio_t dir)
{
    csp_view_t* vw = csp_view(st, ix);
    value_t* vslot;
    vtype_t vt = decl(st,INDEX(ix),vt);
    vtype_t cvt = decl_cfg_vt(decl(st,INDEX(ix),type), vt);
    if (vw->kind == VIEW_HEAP) {  // bit-fields only carry a value, no pin/port
	if (part == PART_VAL)
	    csp_heap_set(st, vw, dir, v);
	// A frame's transport state lives on the BUFFER, not in a value slot,
	// and is a command rather than a value -- so it is not DIN/DOUT
	// shadowed and does not go through the dirty set.
	else if (part == PART_DIR)
	    st->buf[vw->buf].dir = v.i;
	else if (st->buf[vw->buf].transport == TR_CAN) {
	    csp_buf_t* bp = &st->buf[vw->buf];
	    if (part == PART_TX) {
		if (v.i)
		    bp->flags |= BUF_F_TX;
		else
		    bp->flags &= ~BUF_F_TX;
	    }
	    else if (part == PART_DLC) {
		// Clamped, not rejected: the heap holds nbytes and no more, so
		// a longer frame would read past the buffer.
		ivalue_t n = v.i;
		if (n < 0) n = 0;
		if (n > bp->nbytes) n = bp->nbytes;
		bp->dlc = (uint8_t)n;
	    }
	}
	return;
    }
    vslot = csp_slot(st, vw, dir);
    switch(part) {
    case PART_VAL:
	csp_dio_set_val_part(st, vslot, vt, v);
	break;
    case PART_PIN:
	csp_dio_set_pin_part(st, vslot, cvt, v);
	break;
    case PART_PORT:
	csp_dio_set_port_part(st, vslot, cvt, v);
	break;
    case PART_DIR:      // V_DIGITAL/V_ANALOG/V_CAN
	csp_dio_set_dir_part(st, vslot, cvt, v);
	break;
    case PART_PWM:      // V_ANALOG
	if (cvt == V_ANALOG)
	    vslot->a.pwm = v.i;
	break;
    case PART_ENDIAN:   // V_ANALOG/V_CAN
	if (cvt == V_ANALOG)
	    vslot->a.endian = v.i;
	break;
    case PART_PULLUP:   // V_DIGITAL
	if (cvt == V_DIGITAL)
	    vslot->d.pullup = v.i;
	break;
    case PART_PULLDOWN: // V_DIGITAL
	if (cvt == V_DIGITAL)
	    vslot->d.pulldown = v.i;
	break;
    case PART_PERIOD:   // V_TIMER
	if (cvt == V_TIMER)
	    vslot->t.period = v.i;
	break;
    case PART_FIRED:    // V_TIMER
	if (cvt == V_TIMER)
	    vslot->t.fired = v.i;
	break;
    default:
	break;
    }
}

// Get value part from dio (config data & value)
NOINLINE void csp_dio_get_part(csp_rt_t* st, index_t ix, value_t* vp,
			       csp_part_t part, dio_t dir)
{
    csp_view_t* vw = csp_view(st, ix);
    value_t* vslot;
    vtype_t vt = decl(st,INDEX(ix),vt);
    vtype_t cvt = decl_cfg_vt(decl(st,INDEX(ix),type), vt);
    if (vw->kind == VIEW_HEAP) {  // bit-fields only carry a value, no pin/port
	csp_buf_t* bp = &st->buf[vw->buf];
	vp->u = 0;
	switch (part) {
	case PART_VAL: *vp = csp_heap_get(st, vw, dir); break;
	// Direction is a property of the buffer, so it answers for a plain
	// #buffer as well as a CAN frame -- and for a #can field, which reads
	// its frame's direction.
	case PART_DIR: vp->i = bp->dir; break;
	// Frame state, read off the buffer. A #can field answers for its frame
	// too: `A.rx` and `F201.rx` are the same fact.
	case PART_RX:
	    if (bp->transport == TR_CAN)
		vp->i = BOOL(bp->flags & BUF_F_RX);
	    break;
	case PART_TX:
	    if (bp->transport == TR_CAN)
		vp->i = BOOL(bp->flags & BUF_F_TX);
	    break;
	case PART_ID:
	    if (bp->transport == TR_CAN)
		vp->i = (ivalue_t)bp->xref;
	    break;
	case PART_DLC:
	    if (bp->transport == TR_CAN)
		vp->i = bp->dlc;
	    break;
	default: break;
	}
	return;
    }
    vslot = csp_slot(st, vw, dir);
    switch(part) {
    case PART_VAL:
	csp_dio_get_val_part(st, vslot, vt, vp);
	break;
    case PART_PIN:
	csp_dio_get_pin_part(st, vslot, cvt, vp);
	break;
    case PART_PORT:
	csp_dio_get_port_part(st, vslot, cvt, vp);
	break;
    case PART_DIR:      // V_DIGITAL/V_ANALOG/V_CAN
	csp_dio_get_dir_part(st, vslot, cvt, vp);
	break;
    case PART_PWM:      // V_ANALOG
	vp->i = (cvt == V_ANALOG) ? vslot->a.pwm : 0;
	break;
    case PART_ENDIAN:   // V_ANALOG/V_CAN
	vp->i = (cvt == V_ANALOG) ? vslot->a.endian : 0;
	break;
    case PART_PULLUP:   // V_DIGITAL
	vp->i = (cvt == V_DIGITAL) ? vslot->d.pullup : 0;
	break;
    case PART_PULLDOWN: // V_DIGITAL
	vp->i = (cvt == V_DIGITAL) ? vslot->d.pulldown : 0;
	break;
    case PART_PERIOD:   // V_TIMER
	vp->i = (cvt == V_TIMER) ? vslot->t.period : 0;
	break;
    case PART_FIRED:    // V_TIMER
	vp->i = (cvt == V_TIMER) ? vslot->t.fired : 0;
	break;
    default:
	vp->i = 0;
	break;
    }
}

NOINLINE void csp_dio_set(csp_rt_t* st, index_t ix, value_t v, dio_t dir)
{
    csp_view_t* vw = csp_view(st, ix);
    if (vw->kind == VIEW_HEAP) {
	csp_heap_set(st, vw, dir, v);
	return;
    }
    csp_dio_set_val_part(st, csp_slot(st, vw, dir),
			 decl_cfg_vt(decl(st, INDEX(ix), type),
				     decl(st, INDEX(ix), vt)), v);
}

NOINLINE void csp_dio_get(csp_rt_t* st, index_t ix, value_t* vp, dio_t dir)
{
    csp_view_t* vw = csp_view(st, ix);
    if (vw->kind == VIEW_HEAP) {
	*vp = csp_heap_get(st, vw, dir);
	return;
    }
    csp_dio_get_val_part(st, csp_slot(st, vw, dir),
			 decl_cfg_vt(decl(st, INDEX(ix), type),
				     decl(st, INDEX(ix), vt)), vp);
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
    if (n >= (int)st->ps.nn)   // never walk past the last instruction into garbage
	return n;
#if defined(USE_STATISTICS) && (USE_STATISTICS==1)
    st->num_eval0++;
#endif
    op = instr(st, n, op);
    switch(op) {
    case OP_NOP:
	break;
    case OP_LD:
	st->reg[instr(st,n,m.x)] = csp_value(st, instr(st,n,m.mem));
	break;
    case OP_LDP:
	csp_dio_get_part(st, instr(st,n,m.mem), &st->reg[instr(st,n,m.x)],
			 instr(st,n,m.y), DIN);
	break;
    case OP_EQI:
	st->reg[instr(st,n,mi.x)].i =
	    csp_value(st, instr(st,n,mi.mem)).i == instr(st,n,mi.imm);
	break;
    case OP_STI: {  // store immediate to memory (mirror of EQI)
	value_t v;
	v.i = instr(st,n,mi.imm);
	csp_set_value(st, instr(st,n,mi.mem), v);
	break;
    }
    case OP_STIMP:  // same as ST, but marks reactive assignment
    case OP_ST:
	csp_set_value(st, instr(st,n,m.mem), st->reg[instr(st,n,m.x)]);
	break;
    case OP_STP: {
	index_t mm = instr(st,n,m.mem);
	csp_dio_set_part(st, mm, st->reg[instr(st,n,m.x)],
			 instr(st,n,m.y), DOUT);
	bitset_set(st->dset, st_index(st, mm));  // config change must commit
	st->anyd = CSP_TRUE;
	break;
    }
    case OP_CHG: {  // r |= dset[ix]  (force-true on the seed cycle)
	int i = st_index(st, instr(st, n, m.mem));
	st->reg[instr(st,n,m.x)].i |=
	    (st->seed_all || bitset_tst(st->dset, i)) ? 1 : 0;
	break;
    }
    case OP_LI:
	st->reg[instr(st,n,i.x)].i = instr(st,n,i.imm);  // sign extend
	break;
    case OP_LIU:
	st->reg[instr(st,n,i.x)].u = (uint16_t)instr(st,n,i.imm); // zero extend
	break;
    case OP_LIH:
	st->reg[instr(st,n,i.x)].u |= ((uint32_t)(uint16_t)instr(st,n,i.imm)) << 16;
	break;
    case OP_ARG:
	st->arg[instr(st,n,i.imm)] = st->reg[instr(st,n,i.x)];
	break;
    case OP_RULE:
	if (st->reg[instr(st,n,r.cnd)].i)
	    n = n+1;
	else
	    n = n+instr(st,n,r.nxt);  // relative jump
	goto again;
    case OP_INSTATE:  // #in block gate: skip the whole block if State != imm
	if (st->reg[instr(st,n,in.x)].i != instr(st,n,in.imm))
	    return n + instr(st,n,in.nxt);
	n = n+1;
	goto again;
    case OP_NEXT: // rule is done executing
	return n+1;
    case OP_ENTER: // skip y + 2
	return n + instr(st,n,e.num) + 2;
    case OP_NEW:
	// Enter the object like a call -- but only during a full sweep (csp_eval:
	// non-reactive execution and the reactive SEED). csp_react dispatches
	// single rules by ip; if one reaches OP_NEW it must be a no-op, else esp
	// grows unboundedly and corrupts the struct.
	if (st->sweep) {
	    index_t ent = instr(st,n,n.ent);
	    index_t obj = instr(st,n,n.obj);
	    st->stack[st->esp].ix = n+1;      // return address
	    st->stack[st->esp].cur = st->cur;  // store current module
	    st->esp++;
	    st->cur = decl(st, INDEX(obj), mq.m);    // set current module
	    st->offs[CURRENT] = st->offs[st->cur];  // setup locals
	    return INDEX(ent)+1; // first instruction
	}
	break;
    case OP_LEAVE:
	if (st->sweep) {
	    if (st->esp == 0)
		return st->ps.nn; // make it stop
	    st->esp--;
	    st->cur = st->stack[st->esp].cur;
	    n = st->stack[st->esp].ix;
	    st->offs[CURRENT] = st->offs[st->cur];
	    return n;
	}
	break;
    case OP_CALL: {
	// y: function index (low bit: 0=builtin, 1=user), index >> 1
	// z: argument (0/1 arg) or OP_COMMA instruction (2+ args)
	index_t idx = instr(st,n,f.idx);
	uint8_t arity;
	csp_func_fn fn = NULL;

	// Get function pointer
	if (instr(st,n,f.usr)) {
	    if (st->ufuncs && (idx < st->num_ufuncs)) {
		arity = func_arity(st->ufuncs, idx, st->ufuncs_rom);
		fn    = func_fn(st->ufuncs, idx, st->ufuncs_rom);
	    }
	}
	else {
	    if (idx < csp_num_builtin_funcs) {
		arity = func_arity(csp_builtin_funcs, idx, BUILTIN_ROM);
		fn    = func_fn(csp_builtin_funcs, idx, BUILTIN_ROM);
	    }
	}
	if (fn) {
	    value_t val = fn(st, instr(st,n,f.avt), st->arg, arity);
	    st->reg[instr(st,n,f.x)] = val;
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
	    yv = st->reg[instr(st,n,a.y)];
	    xv = eval1(op, yv);
	    break;
	case 2:
	    yv = st->reg[instr(st,n,a.y)];
	    zv = st->reg[instr(st,n,a.z)];
	    xv = eval2(op, yv, zv); //eval_tab2[op](yv,zv);
	    break;
	}
	st->reg[instr(st,n,a.x)] = xv;
	break;
    }
    }
    n = n+1;
    goto again;
}

// mirror dirty leaf buffers between the two heaps (everything lives in the heap)
NOINLINE static void heap_dset_copy(csp_rt_t* st, dio_t to, dio_t from)
{
    int g, i;
    set_group_t bits;

    for (g = 0; g < (int)BITSET_GROUPS(st->view_cap); g++) {
	if ((bits = st->dset[g]) == 0)
	    continue;
	i = g*BITSET_GROUP_BITS;
	while (bits) {
	    if (bits & 1) {
		csp_buf_t* b = &st->buf[st->view[i].buf];
		memcpy(st->heap[to] + b->hp, st->heap[from] + b->hp, b->nbytes);
		// Committing a change into a CAN frame is what makes it due for
		// sending. Free ride: this walk already resolved view -> buffer.
		if ((to == DIN) && (b->transport == TR_CAN))
		    b->flags |= BUF_F_DIRTY;
	    }
	    bits >>= 1;
	    i++;
	}
    }
}

// undo all values (revert dirty out slots to committed values)
void csp_undo(csp_rt_t* st)
{
    if (st->anyd)
	heap_dset_copy(st, DOUT, DIN);
    st->anyd = CSP_FALSE;
    memset(st->dset, 0, BITSET_GROUPS(st->view_cap) * sizeof(set_group_t));
}

// commit changed values to the in buffer
void csp_commit(csp_rt_t* st)
{
    index_t b;
    if (st->anyd)
	heap_dset_copy(st, DIN, DOUT);
    // Promote arrival flags across the commit. A frame received during
    // csp_input landed in the DOUT shadow, so it becomes readable only now --
    // and `.rx` has to become true now too, or a rule guarded on it would read
    // the PREVIOUS frame. One cycle of life, then gone.
    for (b = 0; b < st->nbuf; b++) {
	csp_buf_t* bp = &st->buf[b];
	if (bp->transport != TR_CAN)
	    continue;
	bp->flags &= ~BUF_F_RX;
	if (bp->flags & BUF_F_RXPEND)
	    bp->flags = (bp->flags & ~BUF_F_RXPEND) | BUF_F_RX;
    }
    memset(st->dset, 0, BITSET_GROUPS(st->view_cap) * sizeof(set_group_t));
    st->anyd = CSP_FALSE;
}

// run eval_rule sequentially over an instruction range [start, stop)
index_t csp_eval_range(csp_rt_t* st, index_t start, index_t stop)
{
    index_t n = start;
    index_t x = BAD_INDEX;
    st->sweep = 1;   // full sweep: OP_NEW/LEAVE enter/leave objects
    while(n < stop) {
	n = csp_eval_rule(st, n);
	x = n;
    }
    st->sweep = 0;
    return x;
}

// run eval_rule on all nodes (ROM + RAM), sequentially
index_t csp_eval(csp_rt_t* st)
{
    return csp_eval_range(st, 0, st->ps.nn);
}

// run queue until cycle boundary
index_t csp_react(csp_rt_t* st)
{
#if defined(SUPPORT_REACTIVE) && (SUPPORT_REACTIVE==1)
    index_t x1 = BAD_INDEX;
    if (st->reactive && st->pending_cap) {
	set_group_t* cur = st->pending[st->gen];
	uint32_t ngrp = BITSET_GROUPS(st->pending_cap);
	uint32_t w;
	// Swap generations FIRST: whatever a rule enqueues while we run belongs to
	// the next cycle, so it must land in the other set. (The old code took a
	// snapshot of the queue tail for the same reason.)
	st->gen ^= 1;
	memset(st->pending[st->gen], 0, ngrp * sizeof(set_group_t));

	// Walk the set UPWARDS. A key is (ordinal << obj_shift) | obj and ordinals
	// are handed out in instruction order, so this evaluates rules in the order
	// they are written -- which is exactly what makes a later rule override an
	// earlier one. Empty words are skipped whole, so the cost follows the
	// pending work, not the key space.
	for (w = 0; w < ngrp; w++) {
	    set_group_t bits = cur[w];
	    cur[w] = 0;              // consumed: this set is the free one next cycle
	    while (bits) {
		uint32_t b = (uint32_t)__builtin_ctz(bits);
		index_t  e = (index_t)(w * BITSET_GROUP_BITS + b);
		uint8_t  obj = QENTRY_OBJ(st, e);
		index_t  ip  = st->rule_ip[QENTRY_ORD(st, e)];
		bits &= (bits - 1);              // drop the bit we just took
		// Restore the object context so a module rule's CURRENT-relative
		// field access hits the right instance (sequential does this via
		// OP_NEW; reactive skips NEW). obj 0 = global (offs[0] == 0),
		// leaving global rules unchanged.
		st->cur = obj;
		st->offs[CURRENT] = st->offs[obj];
		csp_eval_rule(st, ip);
		x1 = ip;
	    }
	}
    }
    return x1;
#else
    return BAD_INDEX;
#endif
}

// Run one cycle's evaluation. The transaction model makes reactive and
// sequential yield the SAME committed state -- but only when the WHOLE program
// runs in one mode. A sequential ROM re-asserts its outputs every cycle, so a
// reactive RAM rule that overrides a ROM output would lose once its trigger
// stabilizes (the RAM rule stops firing while the ROM rule keeps writing).
// Hence reactive runs only when the whole program can: no ROM (everything is
// RAM) or the ROM carries its own precomputed graph (rom_n_edg > 0). Otherwise
// fall back to full sequential, which keeps override consistent.
index_t csp_cycle(csp_rt_t* st)
{
    // Pick up anything added since the last rebuild. Two signals: `edited` is
    // set by the add paths, and the rule-body counter is an independent check
    // that costs one comparison and needs no scan -- it catches an emission on
    // a path that forgot to mark. Doing it here, at a cycle boundary, is the
    // only safe point: mid_reset moves every derived table.
    if (st->started && (st->edited || (st->n_rule_emit != st->graph_rules)))
	csp_rebuild(st);

    // First cycle: force OP_CHG true so every <- binding fires once and seeds
    // its initial value. Same boundary as the reactive seed sweep below.
    st->seed_all = (st->cycle <= 1);
#if defined(SUPPORT_REACTIVE) && (SUPPORT_REACTIVE==1)
    if (st->reactive && ((st->rom_nn == 0) || rom_n_edg)) {
	// The reactive queue is change-driven, so it starts empty. Seed it with
	// a full sequential first cycle (csp_set_value enqueues each dependent
	// for the next cycle); run reactively thereafter.
	if (st->cycle <= 1)
	    return csp_eval(st);
	return csp_react(st);
    }
#endif
    return csp_eval(st);   // sequential ROM + RAM (override stays consistent)
}

NOINLINE int lookup_state(csp_rt_t* st, const tstr_t* name)
{
    int i;
    for (i = 0; i < st->ps.ns; i++) {
	if (csp_str_eq(st, st->states[i].name, name->ptr, name->len))
	    return i;
    }
    return -1;
}

// Compare n bytes at a logical string position against a RAM string (memcmp-
// like: 0 == equal). Segment-aware per byte, so it is PROGMEM-safe on AVR where
// the ROM half of the string table lives in flash.
NOINLINE int csp_str_ncmp(csp_rt_t* st, sindex_t pos, const char* s, int n)
{
    int i;
    for (i = 0; i < n; i++) {
	int d = (int)csp_str_byte(st, pos+i) - (uint8_t)s[i];
	if (d) return d;
    }
    return 0;
}

// True when the length-prefixed string at `pos` equals the n-byte RAM string s.
NOINLINE int csp_str_eq(csp_rt_t* st, sindex_t pos, const char* s, int n)
{
    return (csp_str_byte(st, pos-1) == (uint8_t)n) &&
	   (csp_str_ncmp(st, pos, s, n) == 0);
}

// Print the length-prefixed string at logical position `pos`, byte by byte.
NOINLINE void csp_print_str_at(csp_rt_t* st, sindex_t pos)
{
    int len = csp_str_byte(st, pos-1);
    int i;
    for (i = 0; i < len; i++)
	csp_print_char(csp_str_byte(st, pos+i));
}

// look for symbol among nodes in range [start, stop)
NOINLINE static index_t lookup_decl_in(csp_rt_t* st, const tstr_t* name,
				       int start, int stop)
{
    int i = start;
    while(i < stop) {
	int pos = decl(st, i, name);
	if ((pos > 0) && csp_str_eq(st, pos, name->ptr, name->len))
	    return MAKE_INDEX(0,i);
	if (decl(st, i, type) == DECL_MODULE) // skip module def
	    i += (decl(st, i, md.n)+1); // skip elements and END
	i++;
    }
    return BAD_INDEX;
}

// True when decl `di` belongs to the module body currently being parsed. Used
// to decide whether a name resolves to a per-instance member (which has to be
// addressed CURRENT-relative) or to a global (which must NOT be).
NOINLINE int is_module_local(csp_rt_t* st, index_t di)
{
    return (st->mdef != BAD_INDEX) && ((int)INDEX(di) > (int)INDEX(st->mdef));
}

// Lookup for DECLARING a name: only the scope the new decl lands in. A module
// member may shadow a global -- otherwise adding a global later would break
// every module that happens to use that name.
NOINLINE index_t csp_lookup_decl_local(csp_rt_t* st, const tstr_t* name)
{
    int start = (st->mdef != BAD_INDEX) ? INDEX(st->mdef)+1 : 0;
    return lookup_decl_in(st, name, start, st->ps.nd);
}

// Lookup for REFERENCING a name.
NOINLINE index_t csp_lookup_decl(csp_rt_t* st, const tstr_t* name)
{
    if (st->mdef != BAD_INDEX) {
	index_t ix;
	// Module body first, so a local shadows a global of the same name.
	if ((ix = lookup_decl_in(st, name, INDEX(st->mdef)+1, st->ps.nd))
	    != BAD_INDEX)
	    return ix;
	// Then the globals declared before this module. lookup_decl_in skips
	// other modules' bodies, so their members stay private. Without this a
	// module body could not see ANY global -- not a constant, not a timer,
	// nothing.
	return lookup_decl_in(st, name, 0, INDEX(st->mdef));
    }
    return lookup_decl_in(st, name, 0, st->ps.nd);
}

NOINLINE index_t lookup_const(csp_rt_t* st, vtype_t vt, value_t v)
{
    index_t i;
    for (i = 0; i < st->ps.nd; i++) {
	if (IS_CONST(st, i) && (vt == decl(st,i,vt))) {
	    if (decl(st,i,cn.init.u) == v.u)  // binary compare!
		return MAKE_INDEX(0,i);
	}
    }
    return BAD_INDEX;
}

NOINLINE index_t lookup_string_const(csp_rt_t* st, char* str, int slen)
{
    index_t i;
    for (i = 0; i < st->ps.nd; i++) {
	if (IS_CONST(st, i) && (decl(st,i,vt) == V_STRING)) {
	    if (csp_str_eq(st, decl(st,i,cn.init.s), str, slen))
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
    sindex_t pos = st->ps.strp;               // logical position
    sindex_t next = pos + (len+2);
    if ((next - st->rom_strp) >= MAX_STR_BUF) {  // check RAM-local room
	csp_set_error(st, ERR_STRING_SPACE_EXHUSTED);
	return -1;
    }
    st->ps.strp = next;  // allocate
    ram_str_at(st, pos) = len;
    if (len > 0)                  // len==0 (empty string) may pass a NULL name
	memcpy(&ram_str_at(st, pos+1), name, len);
    ram_str_at(st, pos+1+len) = '\0';
    return pos+1;
}

// Find a string in string buffer (ROM + RAM, by logical position)
NOINLINE int lookup_string(csp_rt_t* st, char* name, int name_len)
{
    int pos = 1;  // search from pos=1 in str buf
    while(pos < st->ps.strp) {
	int len = csp_str_byte(st, pos);
	if (csp_str_eq(st, pos+1, name, name_len))
	    return pos+1;
	pos += (len+2);  // length byte and \0
    }
    return -1;
}

// True if `add` more arena bytes still fit inside the usable budget. Used bytes
// = RAM instructions + RAM declarations; this is the byte-level cap that now
// binds before (or alongside) the index-count caps.
NOINLINE static int mem_fits(csp_rt_t* st, size_t add)
{
    size_t ib = (size_t)(st->ps.nn - st->rom_nn) * sizeof(csp_instr_t);
    size_t db = (size_t)(st->ps.nd - st->rom_nd) * sizeof(csp_decl_t);
    return (ib + db + add) <= st->mem_limit;
}

NOINLINE static index_t next_decl_index(csp_rt_t* st)
{
    index_t ix;
    // ps.nd is a LOGICAL count (ROM base + RAM); RAM storage is ram_decl[local].
    // Two caps: the index encoding (MAX_DECLS) and the arena byte budget.
    if ((st->ps.nd - st->rom_nd) >= MAX_DECLS || !mem_fits(st, sizeof(csp_decl_t))) {
	csp_set_error(st, ERR_TOO_MANY_DECLARATIONS);
	return BAD_INDEX;
    }
    ix = MAKE_INDEX(0, st->ps.nd);
    st->ps.nd++;
    return ix;
}

// install a new decl (default to INTEGER 32 bit

NOINLINE index_t csp_new_decl(csp_rt_t* st, const tstr_t* name, decl_t type)
{
    index_t ix;
    int i, pos;

    if ((ix = next_decl_index(st)) == BAD_INDEX)
	return BAD_INDEX;
    pos = 0;
    if (name != NULL) {
	if ((pos = new_string(st, name->ptr, name->len)) < 0)
	    return BAD_INDEX;
    }
    i = INDEX(ix);
    ram_decl_at(st,i)->type = type;
    ram_decl_at(st,i)->name = pos;
    ram_decl_at(st,i)->res = MAKE_RES(8*sizeof(value_t));
    ram_decl_at(st,i)->vt = V_INTEGER;
    return i;
}

// new uniq declaration
NOINLINE index_t csp_new_udecl(csp_rt_t* st, const tstr_t* name, decl_t type)
{
    index_t ix;
    
    if ((ix = csp_lookup_decl_local(st, name)) != BAD_INDEX) {
	if (csp_set_error(st, ERR_ALREADY_DEFINED)) {
	    tstr_t typ = { .ptr = "name", .len = 4 };
	    if (decl(st,ix,type) == type) typ = decl_type_name(type);
	    csp_set_err_arg_tstr(st, 0, &typ);
	    csp_set_err_arg_tstr(st, 1, name);
	}
	return BAD_INDEX;
    }
    return csp_new_decl(st, name, type);
}

// Map a name after '.' to a part selector, PART_LAST if it is not a part.
// Parts are ordinary words disambiguated by position (obj.field wins in code).
NOINLINE static csp_part_t part_from_tstr(const tstr_t* s)
{
    switch (s->len) {
    case 2:
	if (ro_memcmp(s->ptr, s_id, 2) == 0)     return PART_ID;
	if (ro_memcmp(s->ptr, s_rx, 2) == 0)     return PART_RX;
	if (ro_memcmp(s->ptr, s_tx, 2) == 0)     return PART_TX;
	break;
    case 3:
	if (ro_memcmp(s->ptr, s_pin, 3) == 0)    return PART_PIN;
	if (ro_memcmp(s->ptr, s_dir, 3) == 0)    return PART_DIR;
	if (ro_memcmp(s->ptr, s_dlc, 3) == 0)    return PART_DLC;
	break;
    case 4:
	if (ro_memcmp(s->ptr, s_port, 4) == 0)   return PART_PORT;
	break;
    case 5:
	if (ro_memcmp(s->ptr, s_value, 5) == 0)  return PART_VAL;
	if (ro_memcmp(s->ptr, s_fired, 5) == 0)  return PART_FIRED;
	break;
    case 6:
	if (ro_memcmp(s->ptr, s_endian, 6) == 0) return PART_ENDIAN;
	if (ro_memcmp(s->ptr, s_period, 6) == 0) return PART_PERIOD;
	break;
    default:
	break;
    }
    return PART_LAST;
}

NOINLINE index_t new_signed_const(csp_rt_t* st, ivalue_t v)
{
    index_t ix;
    int i;
    const tstr_t empty = { .ptr = NULL, .len = 0};
    if ((ix = csp_new_decl(st,&empty,DECL_CONSTANT)) == BAD_INDEX)
	return BAD_INDEX;
    i = INDEX(ix);
    ram_decl_at(st,i)->cn.init.i = v;
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
    ram_decl_at(st,i)->vt = V_FLOAT;
    ram_decl_at(st,i)->cn.init.f = v;
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
    ram_decl_at(st,i)->res = MAKE_RES(STRING_BITS);
    ram_decl_at(st,i)->vt = V_STRING;
    ram_decl_at(st,i)->cn.init.s = pos;
    return ix;
}

NOINLINE static csp_instr_t* alloc_instr_ptr(csp_rt_t* st,int* pos,opcode_t op)
{
    int i;                        // logical instr index (or the dummy slot)
    csp_instr_t* ip;
    if (st->ap == NULL) {
	i = MAX_INSTRS;           // scratch index (never fetched: ev folds inline)
	ip = &st->imm_scratch;    // write-only dummy for immediate `> expr` eval
    }
    else if ((st->ps.nn - st->rom_nn) >= MAX_INSTRS ||   // index cap, or
	     !mem_fits(st, sizeof(csp_instr_t))) {       // arena byte budget
	csp_set_error(st, ERR_TOO_MANY_INSTRUCTIONS);
	return NULL;
    }
    else {
	i = st->ps.nn++;
	ip = ram_instr_at(st, i);
	// Mirror number_rules: a rule body ends at NEXT/ENTER, so each one opens
	// the next body. The implicit body at the range base is the initial 1.
	if ((op == OP_NEXT) || (op == OP_ENTER))
	    st->n_rule_emit++;
    }
    ip->op = op;
    if (pos != NULL) *pos = i;
    return ip;
}

NOINLINE bool_t asm_RULE(csp_rt_t* st, int* pos, reg_t cnd, int nxt)
{
    csp_instr_t* ip = alloc_instr_ptr(st, pos, OP_RULE);
    if (ip != NULL) {
	ip->r.cnd = cnd;
	ip->r.nxt = nxt;
	return 1;
    }
    return 0;
}

NOINLINE static bool_t asm_NEXT(csp_rt_t* st, int r)
{
    csp_instr_t* ip = alloc_instr_ptr(st, NULL, OP_NEXT);
    if (ip != NULL) {
	ip->x.x = r;
	return 1;
    }
    return 0;
}

// #in <state> block gate: reg x holds the current State (loaded just before);
// nxt is patched at #end to the distance skipping the whole block.
NOINLINE static bool_t asm_INSTATE(csp_rt_t* st, int* pos, reg_t x, int imm)
{
    csp_instr_t* ip = alloc_instr_ptr(st, pos, OP_INSTATE);
    if (ip != NULL) {
	ip->in.x = x;
	ip->in.imm = imm;
	ip->in.nxt = 0;   // patched at #end
	return 1;
    }
    return 0;
}

NOINLINE static bool_t asm_mem_part(csp_rt_t* st, opcode_t op, reg_t x,
				 index_t mem, csp_part_t part)
{
    csp_instr_t* ip = alloc_instr_ptr(st, NULL, op);
    if (ip != NULL) {
	ip->m.y = part;
	ip->m.x = x;
	ip->m.mem = mem;
	return 1;
    }
    return 0;
}

NOINLINE static bool_t asm_memi(csp_rt_t* st, opcode_t op, reg_t x,
				index_t mem, int8_t imm)
{
    csp_instr_t* ip = alloc_instr_ptr(st, NULL, op);
    if (ip != NULL) {
	ip->mi.x = x;
	ip->mi.imm = imm;
	ip->mi.mem = mem;
	return 1;
    }
    return 0;
}

NOINLINE static bool_t asm_EQI(csp_rt_t* st, reg_t x, index_t mem, int8_t imm)
{
    return asm_memi(st, OP_EQI, x, mem, imm);
}

// STI: store a small immediate to memory in one instruction (mirror of EQI).
// x is the register the rule's NEXT points at -- STI never writes it at runtime,
// so it stays a dead slot used only to render the rule body in disassembly.
NOINLINE static bool_t asm_STI(csp_rt_t* st, reg_t x, index_t mem, int8_t imm)
{
    return asm_memi(st, OP_STI, x, mem, imm);
}

// A plain (non-part, non-reactive) store of a small signed-8-bit integer
// immediate can collapse LI+ST into a single STI. Keeps state assignments and
// other small constants to one instruction and one disassembly hit.
static int fits_sti(opcode_t op, const rentry_t* r)
{
    return (op == OP_ST) && r->I && (r->vt != V_FLOAT) &&
	(r->val.i >= TINY_MIN) && (r->val.i <= TINY_MAX);
}

NOINLINE static bool_t asm_mem(csp_rt_t* st, opcode_t op, reg_t x, index_t mem)
{
    return asm_mem_part(st, op, x, mem, PART_VAL);
}

NOINLINE static bool_t asm_imm(csp_rt_t* st, opcode_t op, reg_t x, int16_t imm)
{
    csp_instr_t* ip = alloc_instr_ptr(st, NULL, op);
    if (ip != NULL) {    
	ip->i.x = x;
	ip->i.imm = imm;
	return 1;
    }
    return 0;
}

NOINLINE static bool_t asm_LI(csp_rt_t* st, reg_t x, int16_t imm)
{
    return asm_imm(st, OP_LI, x, imm);
}

NOINLINE static bool_t asm_LIU(csp_rt_t* st, reg_t x, uint16_t imm)
{
    return asm_imm(st, OP_LIU, x, (int16_t)imm);
}

NOINLINE static bool_t asm_LIH(csp_rt_t* st, reg_t x, uint16_t imm)
{
    return asm_imm(st, OP_LIH, x, (int16_t)imm);
}


// Smart load: choose LI, LIU, or LIU+LIH based on value
NOINLINE static bool_t csp_load_int(csp_rt_t* st, reg_t x, ivalue_t val)
{
    if ((val >= -32768) && (val <= 32767)) {
	return asm_LI(st, x, (int16_t)val);
    }
    else {
	uint32_t uval = (uint32_t)val;
	if (!asm_LIU(st, x, (uint16_t)(uval & 0xFFFF)))
	    return 0;
	if (uval > 0xFFFF) {
	    if (!asm_LIH(st, x, (uint16_t)(uval >> 16)))
		return 0;
	}
	return 1;
    }
    return 0;
}

NOINLINE static bool_t csp_load_uint(csp_rt_t* st, reg_t x, uvalue_t val)
{
    if (val <= 32767) {
	return asm_LI(st, x, (int16_t)val);
    }
    else if (val <= 0xFFFF) {
	return asm_LIU(st, x, (uint16_t)val);
    }
    else {
	if (!asm_LIU(st, x, (uint16_t)(val & 0xFFFF)))
	    return 0;
	return asm_LIH(st, x, (uint16_t)(val >> 16));
    }
}

NOINLINE static bool_t csp_load_float(csp_rt_t* st, reg_t x, fvalue_t val)
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
    if (!asm_LIu(st, x, (uint16_t)(v.u & 0xFFFF)))
	return 0;
    return asm_LIH(st, x, (uint16_t)(v.u >> 16));
#endif
}

static int asm_ARG(csp_rt_t* st, reg_t x, int16_t i)
{
    return asm_imm(st, OP_ARG, x, i);
}

NOINLINE static bool_t asm_alu(csp_rt_t* st, opcode_t op,
			       reg_t x, reg_t y, reg_t z)
{
    csp_instr_t* ip = alloc_instr_ptr(st, NULL, op);
    if (ip != NULL) {
	ip->a.x = x;
	ip->a.y = y;
	ip->a.z = z;
	return 1;
    }
    return 0;
}

NOINLINE static bool_t asm_CALL(csp_rt_t* st, reg_t x, int func_idx, int is_user, uint16_t argcode)
{
    csp_instr_t* ip = alloc_instr_ptr(st, NULL, OP_CALL);    
    if (ip != NULL) {
	ip->f.x   = x;
	ip->f.idx = func_idx;
	ip->f.usr = is_user;
	ip->f.avt = argcode;
	return 1;
    }
    return 0;
}

NOINLINE static bool_t asm_ENTER(csp_rt_t* st, int* pos, int n, index_t mx)
{
    csp_instr_t* ip = alloc_instr_ptr(st, pos, OP_ENTER);        
    if (ip != NULL) {
	ip->e.num = n;
	ip->e.mx = mx;
	return 1;
    }
    return 0;
}

NOINLINE static bool_t asm_LEAVE(csp_rt_t* st, int* pos, int n, index_t mx)
{
    csp_instr_t* ip = alloc_instr_ptr(st, pos, OP_LEAVE);
    if (ip != NULL) {
	ip->v.num = n;
	ip->v.mx = mx;
	return 1;
    }
    return 0;
}

NOINLINE static bool_t asm_NEW(csp_rt_t* st, unsigned ent, index_t obj)
{
    csp_instr_t* ip = alloc_instr_ptr(st, NULL, OP_NEW);
    if (ip != NULL) {
	ip->n.ent = ent;
	ip->n.obj = obj;
	return 1;
    }
    return 0;
}

static bool_t asm_bop(csp_rt_t* st, opcode_t op, index_t x ,index_t y, index_t z)
{
    return asm_alu(st, op, x, y, z);
}

static bool_t asm_uop(csp_rt_t* st, opcode_t op, index_t x, index_t y)
{
    return asm_alu(st, op, x, y, 0);
}

static bool_t asm_CVTIF(csp_rt_t* st, index_t x, index_t y)
{
    return asm_uop(st, OP_CVTIF, x, y);
}

static bool_t asm_CVTFI(csp_rt_t* st, index_t x, index_t y)
{
    return asm_uop(st, OP_CVTFI, x, y);
}

static bool_t asm_MOV(csp_rt_t* st, reg_t x, reg_t y)
{
    return asm_uop(st, OP_MOV, x, y);
}

static bool_t asm_AND(csp_rt_t* st, reg_t x, reg_t y, reg_t z)
{
    return asm_bop(st, OP_AND, x, y, z);
}

// compare equal ==
static bool_t asm_EQEQ(csp_rt_t* st, reg_t x, reg_t y, reg_t z)
{
    return asm_bop(st, OP_EQEQ, x, y, z);
}

#if 0
NOINLINE static bool_t asm_NOP(csp_rt_t* st)
{
    return (alloc_instr_ptr(st, NULL, OP_NOP) != NULL);
}

static bool_t asm_OR(csp_rt_t* st, reg_t x, reg_t y, reg_t z)
{
    return asm_bop(st, OP_OR, x, y, z);
}
#endif

// Build reactive dependency graph: declaration -> rules that depend on it
// When a declaration changes, we enqueue all rules that read from it (via LD)
// --- middle-region bump allocator ------------------------------------------
// Reset the middle to the gap between the code growing in from both ends. Called
// once per csp_rebuild; every derived table is then handed out from here, so a
// rebuild simply forgets the old layout instead of freeing eleven blocks.
NOINLINE void csp_mid_reset(csp_rt_t* st)
{
    size_t ib = (size_t)(st->ps.nn - st->rom_nn) * sizeof(csp_instr_t);
    size_t db = (size_t)(st->ps.nd - st->rom_nd) * sizeof(csp_decl_t);
    st->mid_base = CSP_A8(ib + CSP_SCRATCH);
    st->mid      = st->mid_base;
    st->mid_full = 0;
    // The decl end grows DOWN from the top of the pool, so the middle must stop
    // short of it. Guard the subtraction: a program that already fills the pool
    // leaves no middle at all rather than wrapping to a huge end.
    if (st->mem_limit > (db + CSP_SCRATCH + st->mid_base))
	st->mid_end = st->mem_limit - db - CSP_SCRATCH;
    else
	st->mid_end = st->mid_base;
}

// Hand out `n` bytes from the middle, 8-aligned so every table start is aligned
// regardless of what preceded it. NULL when the middle is exhausted -- the caller
// reports it, and mid_full makes the reason visible in /memory rather than
// looking like a mysterious out-of-memory.
NOINLINE void* csp_mid_alloc(csp_rt_t* st, size_t n)
{
    void* p;
    n = CSP_A8(n ? n : 1);
    if ((st->mid + n) > st->mid_end) {
	st->mid_full = 1;
	return NULL;
    }
    p = st->mem + st->mid;
    st->mid += n;
    memset(p, 0, n);
    return p;
}

#if defined(SUPPORT_REACTIVE) && (SUPPORT_REACTIVE==1)
// Number the rule bodies of instruction range [lo,hi) in scan order, continuing
// from ordinal `ord`. A body starts at the range base (code before any NEXT/ENTER
// belongs to it -- csp_csr seeds current_rule with the base for exactly that
// reason) and again after every NEXT/ENTER. Fills rule_ip[ord] = ip when given a
// table, otherwise just counts. Returns the next free ordinal.
//
// Calling this on [0,rom_nn) reproduces the ordinals a baked rom_edg holds: the
// dump ran this same walk over the same instructions with rom_nn == 0.
NOINLINE static int number_rules(csp_rt_t* st, int lo, int hi,
				 index_t* rule_ip, int ord)
{
    int i;
    if (rule_ip != NULL) rule_ip[ord] = lo;
    ord++;                              // the implicit body at the range base
    for (i = lo; i < hi; i++) {
	opcode_t o = instr(st, i, op);
	if ((o == OP_NEXT) || (o == OP_ENTER)) {
	    if (rule_ip != NULL) rule_ip[ord] = i+1;
	    ord++;
	}
    }
    return ord;
}
#endif

void csp_csr(csp_rt_t* st)
{
#if defined(SUPPORT_REACTIVE) && (SUPPORT_REACTIVE==1)
    int i;
    int r_rom = 0;                 // ordinals [0,r_rom) belong to baked ROM rules
    int current_rule = -1;
    int ord = 0;                   // ordinal of the rule body being scanned
    int next_ord = 0;              // next ordinal to hand out
    index_t* wr;                   // pass-3 write cursors (into the idg block)
    // timeout(T)/elapsed(T)/... pass the timer as an immediate index (LI -> ARG
    // -> CALL), not an OP_LD, so track LI values per register and per arg slot to
    // recover the timer a CALL depends on and give it a graph edge.
    index_t reg_imm[MAX_REGS];
    index_t arg_imm[MAX_ARGS];
    int n = st->ps.nd;             // graph nodes = declarations

    // Size the graph to the actual node/edge counts. idg[n], ofs[n+1] and the
    // pass-3 write cursors wr[n] share one bump block (idg is its base). edg is
    // sized after the counting pass. Nothing is freed -- csp_rebuild resets the
    // middle. wr used to be an index_t[MAX_DECLS] local -- 4K of stack at
    // DECL_BITS=11, and the last thing dimensioned by that width.
    st->edg = NULL;
    st->graph_n = 0;               // enq skips the graph until it is fully built
    st->idg = (index_t*)csp_mid_alloc(st, (size_t)(n + (n+1) + n) * sizeof(index_t));
    if (st->idg == NULL) { st->ofs = NULL; return; }  // middle full: no graph
    st->ofs = st->idg + n;
    wr      = st->ofs + (n+1);

    // Number every rule body 0..n_rule-1 and build the ordinal -> ip map. ROM
    // first (so its ordinals match what rom_edg was baked with), then RAM.
    st->rule_ip = NULL;
    st->n_rule = 0;
    {
	int nr;
	if (st->rom_nn > 0)
	    r_rom = number_rules(st, 0, st->rom_nn, NULL, 0);
	nr = number_rules(st, st->rom_nn, st->ps.nn, NULL, r_rom);
	st->rule_ip = (index_t*)csp_mid_alloc(st, (size_t)nr * sizeof(index_t));
	if (st->rule_ip == NULL) { st->ofs = NULL; return; }  // middle full
	if (st->rom_nn > 0)
	    number_rules(st, 0, st->rom_nn, st->rule_ip, 0);
	number_rules(st, st->rom_nn, st->ps.nn, st->rule_ip, r_rom);
	st->n_rule = (index_t)nr;
    }

    // Size the two pending sets to the (ordinal, object) key space. Two things
    // keep it small. Keying by ORDINAL rather than raw ip: by ip the space would
    // be MAX_INSTRS wide per object and almost all holes, since only rule bodies
    // can ever be pending. And sizing the object field to the objects that EXIST
    // rather than to OBJ_BITS: a program with none gets a 0-bit field (one slot
    // per rule) instead of reserving all 32.
    // On failure pending_cap stays 0 and csp_enq drops the mark -- reactive then
    // does nothing, which is visible rather than silently wrong.
    {
	size_t bits, grp;
	st->obj_shift = 0;
	while ((1u << st->obj_shift) < (unsigned)(st->ps.nq + 1))
	    st->obj_shift++;
	bits = (size_t)st->n_rule << st->obj_shift;
	grp  = BITSET_GROUPS(bits);
	st->pending[0] = (set_group_t*)csp_mid_alloc(st, grp * sizeof(set_group_t));
	st->pending[1] = (set_group_t*)csp_mid_alloc(st, grp * sizeof(set_group_t));
	if (!st->pending[0] || !st->pending[1])
	    st->pending_cap = 0;
	else
	    st->pending_cap = (uint32_t)bits;
	st->gen = 0;
    }

    // Clear in-degree counts
    memset(st->idg, 0, n * sizeof(index_t));
    memset(reg_imm, 0, sizeof(reg_imm));
    memset(arg_imm, 0, sizeof(arg_imm));

    // Pass 1: Count how many rules depend on each declaration
    // A rule depends on a declaration if it contains an LD from that declaration
    // Only RAM rules go into the runtime graph; ROM rules run sequentially (or
    // from their own baked graph). Scan from the RAM instruction base.
    current_rule = st->rom_nn;
    for (i = st->rom_nn; i < st->ps.nn; i++) {
	switch (instr(st,i,op)) {
	case OP_RULE:
	    current_rule = -1;
	    break;
	case OP_NEXT:
	    current_rule = i+1;
	    break;
	case OP_LD:
	case OP_CHG:
	    if (current_rule >= 0) {
		index_t mem = INDEX(instr(st,i,m.mem));
		if (mem < st->ps.nd) {
		    st->idg[mem]++;
		}
	    }
	    break;
	case OP_EQI:
	    if (current_rule >= 0) {
		index_t mem = INDEX(instr(st,i,mi.mem));
		if (mem < st->ps.nd) {
		    st->idg[mem]++;
		}
	    }
	    break;
	case OP_LI:
	case OP_LIU:
	    reg_imm[instr(st,i,i.x)] = (index_t)instr(st,i,i.imm);
	    break;
	case OP_ARG:
	    arg_imm[instr(st,i,i.imm)] = reg_imm[instr(st,i,i.x)];
	    break;
	case OP_CALL:   // timer args (timeout(T), ...) become timer -> rule edges
	    if (current_rule >= 0) {
		uint16_t avt = instr(st,i,f.avt);
		int a;
		for (a = 0; a < MAX_ARGS; a++) {
		    if (((avt >> (a*4)) & 0xf) == V_TIMER) {
			index_t mem = INDEX(arg_imm[a]);
			if (mem < st->ps.nd)
			    st->idg[mem]++;
		    }
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

    // Now the total edge count is known -> size edg exactly. It comes out zeroed
    // because pass 3 leaves HOLES: pass
    // 1 counts every reference but pass 3 drops consecutive duplicates, so decl i
    // fills only [ofs[i], ofs[i]+idg[i]) and the rest of its span stays unwritten.
    // enq_elist reads only the filled part, but csp_dump_code bakes the whole
    // array into rom_edg -- leaving it uninitialised put heap garbage in flash.
    {
	size_t edges = st->ofs[st->ps.nd];
	st->edg = (index_t*)csp_mid_alloc(st, (edges ? edges : 1) * sizeof(index_t));
	if (st->edg == NULL) return;   // middle full: leave idg/ofs, no edges
    }

    // Pass 3: Fill in rule ORDINALS for each declaration. edg stores the ordinal,
    // not the ip, so csp_enq needs no lookup (and dumping bakes rom_edg with the
    // same numbering). `ord` walks in lockstep with current_rule, matching the
    // order number_rules handed ordinals out in: the implicit body at the RAM
    // base first (r_rom), then one per NEXT/ENTER.
    memcpy(wr, st->ofs, st->ps.nd * sizeof(index_t));
    memset(reg_imm, 0, sizeof(reg_imm));
    memset(arg_imm, 0, sizeof(arg_imm));

    // Only RAM rules go into the runtime graph; ROM rules run sequentially (or
    // from their own baked graph). Scan from the RAM instruction base.
    current_rule = st->rom_nn;
    ord = r_rom;
    next_ord = r_rom + 1;
    for (i = st->rom_nn; i < st->ps.nn; i++) {
	switch (instr(st,i,op)) {
	case OP_RULE:
	    current_rule = -1;
	    break;
	case OP_NEXT:
	    current_rule = i+1;
	    ord = next_ord++;
	    break;
	case OP_LD:
	case OP_CHG:
	    if (current_rule >= 0) {
		index_t mem = INDEX(instr(st,i,m.mem));
		if (mem < st->ps.nd &&
		    (wr[mem] == st->ofs[mem] || st->edg[wr[mem]-1] != ord))
		    st->edg[wr[mem]++] = ord;
	    }
	    break;
	case OP_EQI:
	    if (current_rule >= 0) {
		index_t mem = INDEX(instr(st,i,mi.mem));
		if (mem < st->ps.nd &&
		    (wr[mem] == st->ofs[mem] || st->edg[wr[mem]-1] != ord))
		    st->edg[wr[mem]++] = ord;
	    }
	    break;
	case OP_LI:
	case OP_LIU:
	    reg_imm[instr(st,i,i.x)] = (index_t)instr(st,i,i.imm);
	    break;
	case OP_ARG:
	    arg_imm[instr(st,i,i.imm)] = reg_imm[instr(st,i,i.x)];
	    break;
	case OP_CALL:   // timer args (timeout(T), ...) become timer -> rule edges
	    if (current_rule >= 0) {
		uint16_t avt = instr(st,i,f.avt);
		int a;
		for (a = 0; a < MAX_ARGS; a++) {
		    if (((avt >> (a*4)) & 0xf) == V_TIMER) {
			index_t mem = INDEX(arg_imm[a]);
			if (mem < st->ps.nd &&
			    (wr[mem] == st->ofs[mem] || st->edg[wr[mem]-1] != ord))
			    st->edg[wr[mem]++] = ord;
		    }
		}
	    }
	    break;
	case OP_ENTER:  // start of object
	    current_rule = i+1;
	    ord = next_ord++;
	    break;
	case OP_LEAVE:  // end of object
	    current_rule = -1;
	    break;
	default:
	    break;
	}
    }
    // Compact. Pass 1 counts every reference but pass 3 keeps only distinct
    // consecutive ones, so each decl's span ends in unwritten holes. Slide the
    // edges down and rebuild ofs/idg dense: enq_elist gets a tighter walk, edg
    // shrinks to the real edge count, and csp_dump_code (which bakes the whole
    // [0,ofs[nd]) span into rom_edg) no longer emits hole padding.
    // Safe in place: the new write cursor never overtakes the read base, since
    // every decl's deduplicated count is <= the counted one.
    {
	index_t w = 0;
	for (i = 0; i < st->ps.nd; i++) {
	    index_t base = st->ofs[i];             // read BEFORE overwriting it
	    index_t cnt  = wr[i] - base;           // edges pass 3 actually wrote
	    index_t j;
	    st->ofs[i] = w;
	    for (j = 0; j < cnt; j++)
		st->edg[w++] = st->edg[base+j];
	    st->idg[i] = cnt;
	}
	st->ofs[st->ps.nd] = w;
	// Give the holes back. edg is the last thing bumped so far, so the cursor
	// can simply be rewound over them -- a bump allocator can free its top.
	st->mid = (size_t)((uint8_t*)st->edg - st->mem) + CSP_A8((size_t)w * sizeof(index_t));
    }
    st->graph_n = n;   // graph is complete: enq may now read it for ix < n

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

NOINLINE static int csp_next_token(csp_rt_t* st, char* str, token_t* tp)
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
	    if (len > MAX_NAME_LEN) {
		if (csp_set_error(st, ERR_NAME_TOO_LONG)) {
		    csp_set_err_arg_int(st, 0, len);
		}
		return -1; // fixme set error code
	    }
	    if ((i = find_tok_entry(name,len)) >= 0)
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
		fvalue_t result;
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
		result = FIX_FROM_INT(v) + frac;
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
NOINLINE int csp_scan_line(csp_rt_t* st, char* str, token_t* tv, size_t* num_toks)
{
    char* str0 = str;
    size_t i;
    size_t max_toks = *num_toks;

    i = 0;
    while(i < max_toks) {
	int n = csp_next_token(st, str, &tv[i]);
	if (n < 0)
	    return -1;
	str += n;
	if ((tv[i].t == NEWLINE) || (tv[i].t == NONE)) {
	    *num_toks = i;
	    return str-str0;
	}
	i++;
    }
    csp_set_error(st, ERR_TOO_MANY_TOKENS);
    return -1;
}

void csp_pstate_save(csp_rt_t* st, csp_pmark_t* pm)
{
    pm->ps          = st->ps;
    pm->mdef        = st->mdef;
    pm->ent         = st->ent;
    pm->sdef        = st->sdef;
    pm->in_marker   = st->in_marker;
    pm->save_sx     = st->save_sx;
    pm->sx          = st->sx;
    pm->cur         = st->cur;
    pm->n_rule_emit = st->n_rule_emit;
}

// Rewind to a mark. Dropping nn/nd/nq/ns/strp back un-writes whatever the
// failed parse emitted: the slots are simply reused by the next parse. Only
// the cursors need explicit restoring.
void csp_pstate_restore(csp_rt_t* st, csp_pmark_t* pm)
{
    // Keep the diagnostics: err/err_args/err_strp describe why we are rewinding
    // and the caller has not necessarily reported them yet. Restoring them would
    // reset err to ERR_OK and the failure would print as "ok".
    csp_err_t err = st->ps.err;
    uintptr_t a0 = st->ps.err_args[0], a1 = st->ps.err_args[1],
	      a2 = st->ps.err_args[2];
    uint32_t esp = st->ps.err_strp;
    uint32_t line = st->ps.line;

    st->ps          = pm->ps;
    st->ps.err      = err;
    st->ps.err_args[0] = a0;
    st->ps.err_args[1] = a1;
    st->ps.err_args[2] = a2;
    st->ps.err_strp = esp;
    st->ps.line     = line;
    st->mdef        = pm->mdef;
    st->ent         = pm->ent;
    st->sdef        = pm->sdef;
    st->in_marker   = pm->in_marker;
    st->save_sx     = pm->save_sx;
    st->sx          = pm->sx;
    st->cur         = pm->cur;
    st->n_rule_emit = pm->n_rule_emit;
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
	    if (INDEX(ix) >= st->rom_nd)   // is_mapped cache is RAM-only
		ram_decl_at(st,INDEX(ix))->is_mapped = 0;
	}
    }
}

// load immedate value.
NOINLINE static bool_t csp_load_value(csp_rt_t* st, reg_t x, vtype_t vt, value_t val)
{
    switch(vt) {
    case V_INDEX:
    case V_TIMER:
    case V_DIGITAL:
    case V_ANALOG:
    case V_CAN:
	// indices are unsigned MAKE_INDEX values: a CURRENT-relative index can
	// exceed int16 positive range, so load unsigned (LIU) -- a signed LI
	// would store it negative and misrender on disassembly.
	return csp_load_uint(st, x, val.u);
    case V_INTEGER:
	return csp_load_int(st, x, val.i);
    case V_UNSIGNED:
	return csp_load_uint(st, x, val.u);
    case V_FLOAT:
	return csp_load_float(st, x, val.f);
    case V_STRING:
	return asm_LI(st, x, val.s);  // load string index
    default:
	return 0;
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
    // The is_mapped/reg register cache lives in the decl, so it is only usable
    // for RAM decls; a ROM decl (read-only flash) simply allocates a fresh reg.
    int rom = (INDEX(ix) < st->rom_nd);

    if ((ap = st->ap) != NULL) {
	// Check if already mapped AND mapping is still valid
	if (!rom && decl(st,INDEX(ix),is_mapped)) {
	    reg_t r = decl(st,INDEX(ix),reg);
	    if (st->ap->rmap[r] == ix)
		return r;  // mapping still valid
	    // Stale mapping - clear it
	    ram_decl_at(st,INDEX(ix))->is_mapped = 0;
	}
	dst = alloc_reg(st);
	if (!rom) {
	    ram_decl_at(st,INDEX(ix))->is_mapped = 1;
	    ram_decl_at(st,INDEX(ix))->reg = dst;
	}
	ap->rmap[dst] = ix;
	if (decl(st,INDEX(ix),type) == DECL_CONSTANT) {
	    value_t val = decl(st,INDEX(ix),cn.init);
	    vtype_t vt = decl(st,INDEX(ix),vt);
	    if (!csp_load_value(st, dst, vt, val))
		return -1;
	    return dst;
	}
	// generate LD instruction for variables, track for <- rules
	if (!asm_mem(st,OP_LD,dst,ix))
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
	    if (rp->part != PART_VAL) {  // config part: fresh LDP, never cached
		r = alloc_reg(st);
		if (!asm_mem_part(st, OP_LDP, r, rp->ix, rp->part))
		    return -1;
	    }
	    else if ((r = map_reg(st, rp->ix)) < 0)
		return -1;
	}
	else if (rp->I) {
	    r = alloc_reg(st);
	    if (!csp_load_value(st, r, rp->vt, rp->val))
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

    if (decl(st,INDEX(ix),type) == DECL_CONSTANT) {
	I = 1;
	val = decl(st,INDEX(ix),cn.init);
    }
    else if (decl(st,INDEX(ix),type) == DECL_VARIABLE) {
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

// Push a config-part L-value (<var> '.' <part> '='): defer to the store, don't
// fold to a read the way push_part does -- process_assign turns it into an STP
// (or a direct csp_dio_set_part in immediate mode).
NOINLINE static int push_lval_part(rentry_t* rstack, int ep, index_t ix,
				   csp_part_t part, vtype_t vt)
{
    rstack[ep] = (rentry_t) { .ix=ix,.X=1,.L=0,.I=0,.part=part,.vt=vt };
    return ep+1;
}

// Push a config-part read (<var> '.' <part>), e.g. Led.pin, Frame.endian.
// During eval fold it to the current part value; otherwise defer to an LDP.
NOINLINE static int push_part(csp_rt_t* st, rentry_t* rstack, int ep,
			      index_t ix, csp_part_t part)
{
    if (st->ev) {
	value_t pv;
	csp_dio_get_part(st, ix, &pv, part, DIN);
	return push_imm(st, rstack, ep, V_INTEGER, pv);
    }
    rstack[ep] = (rentry_t){ .ix=ix, .L=0, .I=0, .X=1, .part=part,
			     .vt=V_INTEGER };
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
NOINLINE static bool_t coerce_to_float(csp_rt_t* st, rentry_t* e)
{
    rentry_t ent = *e;

    if (ent.vt == V_FLOAT) return 1;  // already float
    if (ent.vt != V_INTEGER) return 0;  // can only convert int

    // For variables (X=1), load first then convert
    if (ent.X && st->ap) {
	if (csp_load(st, &ent) < 0)
	    return 0;
    }

    if (ent.I && !ent.X) {  // pure immediate, not variable
	ent.val.f = op_CVTIF(ent.val.i);
	ent.I = 1;
	ent.L = 0;
    }
    else if (ent.L && st->ap) {
	reg_t r = alloc_reg(st);
	if (!asm_CVTIF(st, r, ent.reg))
	    return 0;
	free_reg(st, ent.reg);
	ent.reg = r;
	ent.L = 1;
	ent.I = 0;
    }
    ent.vt = V_FLOAT;
    *e = ent;
    return 1;
}

// Convert operand to int (float→int via cvtfi)
NOINLINE static bool_t coerce_to_int(csp_rt_t* st, rentry_t* e)
{
    rentry_t ent = *e;

    if (ent.vt == V_INTEGER) return 1;  // already int
    if (ent.vt != V_FLOAT) return 0;  // can only convert float

    // For variables (X=1), load first then convert
    if (ent.X && st->ap) {
	if (csp_load(st, &ent) < 0)
	    return 0;
    }

    if (ent.I && !ent.X) {  // pure immediate, not variable
	ent.val.i = op_CVTFI(ent.val.f);
	ent.I = 1;
	ent.L = 0;
    }
    else if (ent.L && st->ap) {
	reg_t r = alloc_reg(st);
	if (!asm_CVTFI(st, r, ent.reg))
	    return 0;
	free_reg(st, ent.reg);
	ent.reg = r;
	ent.L = 1;
	ent.I = 0;
    }
    ent.vt = V_INTEGER;
    *e = ent;
    return 1;
}

// Coerce rhs value to the declared type of assignment target ix
NOINLINE static bool_t coerce_assign(csp_rt_t* st, index_t ix, rentry_t* e)
{
    vtype_t lt = decl(st,INDEX(ix),vt);

    if (e->vt == V_UNSIGNED)  // same representation as int
	e->vt = V_INTEGER;
    if ((lt == V_FLOAT) && (e->vt == V_INTEGER))
	return coerce_to_float(st, e);
    if (((lt == V_INTEGER) || (lt == V_UNSIGNED)) && (e->vt == V_FLOAT))
	return coerce_to_int(st, e);
    return 1;
}

// Process binary assignment operator: generates ST instruction
// Returns new ep on success, -1 on error
NOINLINE static int process_assign(csp_rt_t* st, opcode_t op, rentry_t* rstack, int ep)
{
    rentry_t lhs = rstack[ep-2];
    rentry_t rhs = rstack[ep-1];

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

    // Type conversion to declared target type if needed
    if (!coerce_assign(st, lhs.ix, &rhs))
	return -1;

    // Compiled path: collapse LI+ST into a single STI for a small immediate
    // plain-value store (mirror of EQI). Keeps e.g. State=OFF one instruction.
    if (st->ap && (lhs.part == PART_VAL) && fits_sti(op, &rhs)) {
	if (!asm_STI(st, 0, lhs.ix, (int8_t)rhs.val.i))
	    return -1;
	rstack[ep-2] = rhs;   // result is the rhs (for chaining A=B=1)
	return ep - 1;
    }

    if (csp_load(st, &rhs) < 0)
	return -1;

    if (!rhs.L && st->ap) {
	// is this an internal error?
	csp_set_error(st, ERR_SYNTAX);  // rhs must have value
	return -1;
    }

    if (!st->ap) {
	if (rhs.I) {
	    if (lhs.part != PART_VAL) {  // <var> '.' <part> = imm  (config write)
		csp_dio_set_part(st, lhs.ix, rhs.val, lhs.part, DOUT);
		bitset_set(st->dset, st_index(st, lhs.ix)); // must commit
		st->anyd = CSP_TRUE;
	    }
	    else
		csp_set_value(st, lhs.ix, rhs.val);
	}
    }
    else { // Generate store instruction
	if (lhs.part != PART_VAL) {  // <var> '.' <part> = rhs  -> STP
	    if (!asm_mem_part(st, OP_STP, rhs.reg, lhs.ix, lhs.part))
		return -1;
	}
	else if (!asm_mem(st, op, rhs.reg, lhs.ix))
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
		if ((at == V_INTEGER) && !coerce_to_float(st, a))
		    return PARSE_ERROR;
		if ((bt == V_INTEGER) && !coerce_to_float(st, b))
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
			if (!asm_bop(st, op, dst, a->reg, b->reg))
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
		    if (!asm_uop(st, op, dst, a->reg))
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
NOINLINE static int process_fcall(csp_rt_t* st, const token_t* word,
				  uint8_t arity, rentry_t* rstack, int ep)
{
    int dst, n, j;
    const csp_func_t* func = NULL;
    uint16_t argcode = 0;
    uint8_t argimm = 0;
    // int func_res;
    int is_user;
    int func_idx;
    int from;                           // func table in ROM?
    rentry_t* rarg = &rstack[ep-arity]; // first arg
    value_t dval = {.u = 0};   // result value when folded; 0 keeps it defined
    int imm = 0;

    if ((func = csp_match_func(st, &word->v.str, arity,
			       rarg, &is_user, &func_idx)) == NULL)
	return -1;
    from = is_user ? st->ufuncs_rom : BUILTIN_ROM;
    // FIXME: handle, changed(x), timeout(t) whith ops
    n = arity;
    for (j = 0; j < n; j++) {
	rentry_t arg = rarg[j];
	vtype_t argvt = arg.vt;
	vtype_t argtype = fn_type(func, j, from); // read RO data!

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
	    if (!coerce_to_int(st, &arg))
		goto type_mismatch;
	    break;
	case V_FLOAT:
	    if (!coerce_to_float(st, &arg))
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
	if (!asm_ARG(st, arg.reg, j)) {
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
    // Fold when all args are immediate AND either the function is pure, or we
    // are in eval mode (st->ev, i.e. an immediate `> expr` at the prompt) where
    // even an impure call like latch()/print() must run now for its side effect
    // -- otherwise the emitted OP_CALL is never executed and the call no-ops.
    if (imm && (func_pure(func,0,from) || st->ev)) {
	value_t arg[MAX_ARGS];
	csp_func_fn fn = NULL;

	if (is_user)
	    fn = func_fn(st->ufuncs, func_idx, st->ufuncs_rom);
	else
	    fn = func_fn(csp_builtin_funcs, func_idx, BUILTIN_ROM);
	for (j = 0; j < arity; j++)
	    arg[j] = rarg[j].val;
	dval = fn(st, argcode, arg, arity);
    }

    // pop rstack
    if (n > 0) {
	ep -= n;
    }
    dst = alloc_reg(st);
    if (!asm_CALL(st, dst, func_idx, is_user, argcode))
	return -1;
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
NOINLINE index_t make_buf_view(csp_rt_t* st, index_t parent,
			       ivalue_t b0, ivalue_t b1);

NOINLINE int csp_parse_expr(csp_rt_t* st, const token_t* tv, size_t* num_toks,
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
	goto after_primary;
    case FLT:
	if ((ep = push_imm(st, rstack, ep, V_FLOAT, tval.val)) < 0)
	    return 0;
	ptok = FLT;
	goto after_primary;
    case STR:
	if ((ep = push_str(st, rstack, ep, tval.str.ptr, tval.str.len)) < 0)
	    return 0;
	ptok = STR;
	goto after_primary;
    case IN:  case OUT:  case INOUT:       // dir keyword as int  (Led.dir=out)
    case NATIVE: case LITTLE: case BIG: {  // endian keyword as int
	value_t kv;
	kv.i = (tok==IN)    ? DIR_IN  : (tok==OUT)    ? DIR_OUT   :
	       (tok==INOUT) ? DIR_INOUT :
	       (tok==NATIVE) ? E_NATIVE : (tok==LITTLE) ? E_LITTLE : E_BIG;
	if ((ep = push_imm(st, rstack, ep, V_INTEGER, kv)) < 0)
	    return 0;
	ptok = INT;
	goto after_primary;
    }
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
	    if ((ix = csp_lookup_decl(st,&tval.str)) == BAD_INDEX) {
		int s = lookup_state(st, &tval.str);
		if (s >= 0) {
		    value_t sv;
		    sv.i = st->states[s].snum;
		    if ((ep = push_imm(st, rstack, ep, V_INTEGER, sv)) < 0)
			return 0;
		    ptok = INT;
		    goto after_primary;
		}
		if (csp_set_error(st, ERR_VARIABLE_NOT_DECLARED)) {
		    csp_set_err_arg_tstr(st, 0, &tval.str);
		}
		return 0;
	    }
	    // Handle obj.field access
	    if ((decl(st,INDEX(ix),type) == DECL_OBJECT) &&
		(tv[i].t == DOT) && (tv[i+1].t == WORD)) {
		index_t mx = decl(st,INDEX(ix),mq.mx);  // module def
		ivalue_t dn = decl(st,INDEX(mx),md.n);  // number of elements
		index_t jx;
		tval = tv[i+1].v;
		if ((jx = lookup_decl_in(st, &tval.str,
					 INDEX(mx)+1,INDEX(mx)+1+dn)) == BAD_INDEX) {
		    if (csp_set_error(st, ERR_FIELD_NOT_FOUND)) {
			csp_set_err_arg_tstr(st, 0, &tval.str);
		    }
		    return 0;
		}
		ix = MAKE_INDEX(decl(st,INDEX(ix),mq.m),INDEX(jx));
		i += 2;
	    }
	    // Apply module context
	    if ((OBJ(ix) == 0) && is_module_local(st, ix))
		ix = MAKE_INDEX(CURRENT, INDEX(ix));

	    // Buf[pos] / Buf[pos0..pos1] -- byte access on a buffer
	    if ((i < n) && (tv[i].t == LB) &&
		(decl(st,INDEX(ix),type) == DECL_BUFFER)) {
		ivalue_t p0, p1;
		i++;                                   // '['
		if (tv[i].t != INT) { csp_set_error(st, ERR_SYNTAX); return 0; }
		p0 = p1 = tv[i].v.val.i; i++;
		if ((tv[i].t == DOT) && (tv[i+1].t == DOT)) {
		    i += 2;
		    if (tv[i].t != INT) {csp_set_error(st,ERR_SYNTAX); return 0;}
		    p1 = tv[i].v.val.i; i++;
		}
		if (tv[i].t != RB) { csp_set_error(st, ERR_SYNTAX); return 0; }
		i++;                                   // ']'
		if ((ix = make_buf_view(st, ix, p0*8, (p1+1)*8-1)) == BAD_INDEX)
		    return 0;
	    }

	    // <var> '.' <part>  -- config part read (obj.field handled above)
	    if ((i+1 < n) && (tv[i].t == DOT) && (tv[i+1].t == WORD) &&
		(decl(st,INDEX(ix),type) != DECL_OBJECT)) {
		csp_part_t pt = part_from_tstr(&tv[i+1].v.str);
		if (pt != PART_LAST) {
		    i += 2;
		    // '=' / '<-' after the part -> assignment target (STP), else
		    // a plain part read (LDP / eval-fold).
		    if ((i < n) && ((tv[i].t == EQ) || (tv[i].t == RIMP))) {
			ep = push_lval_part(rstack, ep, ix, pt,
					    decl(st,INDEX(ix),vt));
		    }
		    else if ((ep = push_part(st, rstack, ep, ix, pt)) < 0)
			return 0;
		    ptok = WORD;
		    goto after_primary;
		}
	    }

	    // Check if this is an l-value (assignment target)
	    vt = decl(st,INDEX(ix),vt);
	    if ((i < n) && ((tv[i].t == EQ)||(tv[i].t == RIMP))) {
		// L-value: push index only, no load
		ep = push_lval(rstack, ep, ix, vt);
	    }
	    else {
		if ((ep = push_var(st, rstack, ep, ix, vt)) < 0)
		    return 0;
	    }
	    ptok = WORD;
	    goto after_primary;
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

after_primary:
    // After parsing a primary (INT, FLT, STR, WORD), check if next token
    // can continue the expression. If next is another primary, terminate.
    if (i < n) {
	switch (tv[i].t) {
	case INT: case FLT: case STR: case WORD:
	    goto out;  // next primary terminates expression
	default:
	    break;
	}
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
	// printf("PARSE_EXPR tokens=%d\n", i);
	*num_toks = i;
	if (result)
	    *result = rstack[0];
	return 1;
    }
    return 0;
}

// parse expr while turn of codegen is the same as partial eval
NOINLINE int csp_parse_const_expr(csp_rt_t* st,
				  const token_t* tv, size_t* num_toks,
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
    ivalue_t res;
} res_param_t;

static const uint8_t pat_res[] = {
    P_OPT,6,
      P_TOK,COLON,
      P_INTEGER_S, csp_offsetof(res_param_t,res), STOP_RES,
    P_OPT_END,
    P_END
};

typedef struct {
    ivalue_t port;
    ivalue_t pin;
} port_pin_t;

// sub-pattern for <port> ':' <pin> | <pin>
// SCANED BEFORE use is scanned
static const uint8_t pat_port_pin[] = {
    P_CHOICE, 2,
      P_ALT, 9, // Alt 1: port:pin
        P_INTEGER_S, csp_offsetof(port_pin_t, port), STOP_PORT,
	P_TOK, COLON,
	P_INTEGER_S, csp_offsetof(port_pin_t, pin), STOP_PIN1,
      P_ALT_END,
      P_ALT, 4, // Alt 2: pin
        P_INTEGER_S, csp_offsetof(port_pin_t, pin), STOP_PIN2,
      P_ALT_END,
    P_CHOICE_END,
    P_END
};

typedef struct {
    tstr_t name;
} module_param_t;

// '#' 'module' <name>
static const uint8_t pat_module[] = {
    P_STR, csp_offsetof(module_param_t, name),
    P_END
};

NOINLINE int csp_parse_module(csp_rt_t* st, token_t* tv, int ti, size_t n)
{
    module_param_t d;
    index_t ix;
    int jx;
    int i;

    if (pmatch(st, tv, ti, n, pat_module, &d, sizeof(d)) < 0) {
	csp_set_error(st, ERR_SYNTAX);
	return -1;
    }
    // Mark before anything is emitted: this is where an aborted module rewinds to.
    csp_pstate_save(st, &st->mod_mark);
    if ((ix = csp_new_udecl(st, &d.name, DECL_MODULE)) == BAD_INDEX)
	return -1;
    {
	// create a local state variable (if states are supported)
	// maybe only if #states are defined in module context?
	const tstr_t State = { .ptr = "State", .len = 5};
	index_t ix;
	st->save_sx = st->sx;
	ix = csp_new_decl(st, &State, DECL_VARIABLE);
	st->sx = MAKE_INDEX(CURRENT, INDEX(ix));
    }

    st->mdef = ix;  // current module being defined
    if (!asm_ENTER(st, &jx, 0, ix))
	return -1;
    st->ent = jx;   // entry point of module being defined
    i = INDEX(ix);
    ram_decl_at(st,i)->md.n = 0;
    ram_decl_at(st,i)->md.ent = st->ent;
    return 0;
}

typedef struct {
} end_param_t;

// '#' 'end' [....]
static const uint8_t pat_end[] = {
    P_END
};

NOINLINE int csp_parse_end(csp_rt_t* st, token_t* tv, int ti, size_t n)
{
    end_param_t d;
    index_t mx, ex;
    int lx;
    const tstr_t empty = { .ptr = NULL, .len = 0};
    
    if (pmatch(st, tv, ti, n, pat_end, &d, sizeof(d)) < 0) {
	csp_set_error(st, ERR_SYNTAX);
	return -1;
    }

    if (st->sdef >= 0) {
	// close the #in block: patch OP_INSTATE.nxt to jump past everything
	// emitted since the gate, so a State mismatch skips the whole block.
	ram_instr_at(st, st->in_marker)->in.nxt = st->ps.nn - st->in_marker;
	st->sdef = -1;
	return 0;
    }
    if ((mx = st->mdef) == BAD_INDEX) {
	csp_set_error(st, ERR_END_MISMATCH);
	return -1;  // no module
    }
    if ((ex = csp_new_decl(st, &empty, DECL_END)) == BAD_INDEX)
	return -1;
    ram_decl_at(st, INDEX(mx))->md.n = (INDEX(ex) - INDEX(mx)) - 1;
    if (!asm_LEAVE(st, &lx, 0, 0))
	return -1;
    // ent MUST be OP_ENTER!
    ram_instr_at(st, st->ent)->e.num = (lx - st->ent - 1);
    ram_instr_at(st, lx)->v.num = instr(st, st->ent, e.num);
    ram_instr_at(st, lx)->v.mx  = instr(st, st->ent, e.mx);
    // stack?
    st->mdef = BAD_INDEX;
    st->ent = 0;
    st->sx   = st->save_sx;
    return 0;
}

// '#' 'variable' <name>[':' <size>] [<opt>+] ['=' <num>]
// <opt> := 'in'|'out'|'inout'|  -- when use as argument in module
//          'signed'|'unsigned'|'float'
// Note that in is used for input arguments in objects
// and out is used for output argumets

typedef struct {
    tstr_t name;
    res_param_t r;
    decl_opts_t opts;
    value_t init;
} variable_param_t;

static const uint8_t pat_variable[] = {
    P_STR, csp_offsetof(variable_param_t, name),
    P_PAT, PAT_RES, csp_offsetof(variable_param_t, r), STOP_VAR_RES_CONT,
    P_OPTS, csp_offsetof(variable_param_t, opts),
    P_OPT, 6, P_TOK, EQ,
    P_NUMBER_S,
      csp_offsetof(variable_param_t, opts), // pick up vt here
      csp_offsetof(variable_param_t, init),
      STOP_VAR_INIT,
    P_OPT_END,
    P_END
};

//
NOINLINE int csp_parse_variable(csp_rt_t* st, token_t* tv, int ti, size_t n)
{
    variable_param_t d = {0};
    index_t ix;
    int i, r;

    // set default values
    d.r.res = 8*sizeof(ivalue_t);
    d.opts.vt = V_INTEGER;

    if ((r = pmatch(st, tv, ti, n, pat_variable, &d, sizeof(d))) < 0) {
	csp_set_error(st, ERR_SYNTAX);
	return -1;
    }
    if ((ix = csp_new_udecl(st, &d.name, DECL_VARIABLE)) == BAD_INDEX)
	return -1;
    i = INDEX(ix);
    ram_decl_at(st,i)->vt = d.opts.vt;
    ram_decl_at(st,i)->res = MAKE_RES(d.r.res);
    ram_decl_at(st,i)->dir = d.opts.dir;
    ram_decl_at(st,i)->va.init = d.init;

    // optional:  bind <buffer> '[' <bit0> ['..' <bit1>] ']'
    // a bound variable is a bit-field view into a buffer (bits, not bytes)
    if ((r < (int)n) && (tv[r].t == WORD) && (tv[r].v.str.len == 4) &&
	(memcmp(tv[r].v.str.ptr, "bind", 4) == 0)) {
	index_t bx;
	ivalue_t b0, b1;
	int j = r + 1;
	if ((j >= (int)n) || (tv[j].t != WORD) ||
	    ((bx = csp_lookup_decl(st, &tv[j].v.str)) == BAD_INDEX) ||
	    (decl(st,INDEX(bx),type) != DECL_BUFFER)) {
	    csp_set_error(st, ERR_SYNTAX); return -1;
	}
	j++;
	if ((j >= (int)n) || (tv[j].t != LB) ||
	    (j+1 >= (int)n) || (tv[j+1].t != INT)) {
	    csp_set_error(st, ERR_SYNTAX); return -1;
	}
	j += 2;
	b0 = b1 = tv[j-1].v.val.i;
	if ((j+1 < (int)n) && (tv[j].t == DOT) && (tv[j+1].t == DOT)) {
	    j += 2;
	    if ((j >= (int)n) || (tv[j].t != INT)) {
		csp_set_error(st, ERR_SYNTAX); return -1;
	    }
	    b1 = tv[j].v.val.i; j++;
	}
	if ((j >= (int)n) || (tv[j].t != RB)) {
	    csp_set_error(st, ERR_SYNTAX); return -1;
	}
	ram_decl_at(st,i)->bound  = 1;
	ram_decl_at(st,i)->ca.id  = INDEX(bx);
	ram_decl_at(st,i)->ca.bit = b0;
	ram_decl_at(st,i)->ca.len = MAKE_CAN_LEN((b1-b0)+1);
	ram_decl_at(st,i)->ca.endian = d.opts.endian;
    }
    return 0;
}

// '#' 'constant' <name>[':' <size>] [<opt>+] '=' <num>
typedef struct {
    tstr_t name;
    res_param_t r;
    decl_opts_t opts;
    value_t init;
} constant_param_t;

static const uint8_t pat_constant[] = {
    P_STR, csp_offsetof(constant_param_t, name),
    P_PAT, PAT_RES, csp_offsetof(constant_param_t, r), STOP_CONST_RES_CONT,
    P_OPTS, csp_offsetof(constant_param_t, opts),
    P_TOK, EQ,
    P_NUMBER_S,
    csp_offsetof(constant_param_t, opts), // pick up vt here
    csp_offsetof(constant_param_t, init),
    STOP_CONST_INIT,
    P_END
};

NOINLINE int csp_parse_constant(csp_rt_t* st, token_t* tv, int ti, size_t n)
{
    constant_param_t d = {0};
    index_t ix;
    int i;

    // set default values
    d.r.res = 8*sizeof(ivalue_t);
    d.opts.vt = V_INTEGER;

    if (pmatch(st, tv, ti, n, pat_constant, &d, sizeof(d)) < 0) {
	csp_set_error(st, ERR_SYNTAX);
	return -1;
    }
    if ((ix = csp_new_udecl(st, &d.name, DECL_CONSTANT)) == BAD_INDEX)
	return -1;
    i = INDEX(ix);
    ram_decl_at(st,i)->vt = d.opts.vt;
    ram_decl_at(st,i)->res = MAKE_RES(d.r.res);
    ram_decl_at(st,i)->cn.init = d.init;
    return 0;
}


// '#' 'digital' <name> [<iodir>|<pull>] [<port>':']<pin>
typedef struct {
    tstr_t name;
    port_pin_t port_pin;
    decl_opts_t opts;
} digital_param_t;

static const uint8_t pat_digital[] = {
    P_STR, csp_offsetof(digital_param_t, name),
    P_OPTS, csp_offsetof(digital_param_t, opts),
    P_PAT, PAT_PORT_PIN, csp_offsetof(digital_param_t, port_pin), STOP_DIGITAL_PP_CONT,
    P_END
};

NOINLINE int csp_parse_digital(csp_rt_t* st, token_t* tv, int ti, size_t n)
{
    digital_param_t d = {0};
    index_t ix;
    int i;

    if (pmatch(st, tv, ti, n, pat_digital, &d, sizeof(d)) < 0) {
	csp_set_error(st, ERR_SYNTAX);
	return -1;
    }
    if (d.opts.dir == 0) d.opts.dir = DIR_IN;

    if ((ix = csp_new_udecl(st, &d.name, DECL_DIGITAL)) == BAD_INDEX)
	return -1;    
    i = INDEX(ix);
    ram_decl_at(st,i)->res = MAKE_RES(1);
    ram_decl_at(st,i)->di.pin = d.port_pin.pin;
    ram_decl_at(st,i)->di.port = d.port_pin.port;
    ram_decl_at(st,i)->dir = d.opts.dir;
    ram_decl_at(st,i)->di.pullup = d.opts.pullup;
    ram_decl_at(st,i)->di.pulldown = d.opts.pulldown;
    return 0;
}

typedef struct {
    tstr_t name;
    res_param_t r;
    port_pin_t port_pin;    
    decl_opts_t opts;
} analog_param_t;

static const uint8_t pat_analog[] = {
    P_STR, csp_offsetof(analog_param_t, name),
    P_PAT, PAT_RES, csp_offsetof(analog_param_t, r), STOP_ANALOG_RES_CONT,
    P_OPTS, csp_offsetof(analog_param_t, opts),
    P_PAT, PAT_PORT_PIN, csp_offsetof(analog_param_t, port_pin), STOP_ANALOG_PP_CONT,
    P_END
};

//'#' 'analog' <name> [':'<size>] [<opt>*]  [<port>':'] <pin>
//   <opt> := 'in' | 'out' | 'inout' | 'pwm' | 'float' | 'signed' | 'unsigned'
NOINLINE int csp_parse_analog(csp_rt_t* st, token_t* tv, int ti, size_t n)
{
    analog_param_t d = {0};
    index_t ix;
    int i;

    d.r.res = 10;
    d.opts.vt = V_INTEGER;
    if (pmatch(st, tv, ti, n, pat_analog, &d, sizeof(d)) < 0) {
	csp_set_error(st, ERR_SYNTAX);
	return -1;
    }
    if (d.opts.dir == 0) d.opts.dir = DIR_IN;

    if ((ix = csp_new_udecl(st, &d.name, DECL_ANALOG)) == BAD_INDEX)
	return -1;
    i = INDEX(ix);
    ram_decl_at(st,i)->vt = d.opts.vt;
    ram_decl_at(st,i)->res = MAKE_RES(d.r.res);
    ram_decl_at(st,i)->an.pin = d.port_pin.pin;
    ram_decl_at(st,i)->an.port = d.port_pin.port;
    ram_decl_at(st,i)->dir = d.opts.dir;
    ram_decl_at(st,i)->an.pwm = d.opts.pwm;
    ram_decl_at(st,i)->an.endian = d.opts.endian;    
    return 0;
}

typedef struct {
    tstr_t name;
    ivalue_t timeout;
    ivalue_t init;
} timer_param_t;

static const uint8_t pat_timer[] = {
    P_STR, csp_offsetof(timer_param_t, name),
    P_INTEGER_S, csp_offsetof(timer_param_t, timeout), STOP_TIMER_TMO,
    P_OPT, 6,
      P_TOK, EQ,
      P_INTEGER_S, csp_offsetof(timer_param_t, init), STOP_TIMER_INIT,
    P_OPT_END,
    P_END
};

NOINLINE int csp_parse_timer(csp_rt_t* st, token_t* tv, int ti, size_t n)
{
    timer_param_t d = {0};
    index_t tm, tx;
    int i;
    const tstr_t empty = { .ptr = NULL, .len = 0};

    d.init = 0;
    if (pmatch(st, tv, ti, n, pat_timer, &d, sizeof(d)) < 0) {
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
    ram_decl_at(st,i)->vt = V_UNSIGNED;
    ram_decl_at(st,i)->res = MAKE_RES(32);
    ram_decl_at(st,i)->va.init.u = 0;

    i = INDEX(tm);
    ram_decl_at(st,i)->vt = V_TIMER;
    ram_decl_at(st,i)->tm.fired = 0;
    ram_decl_at(st,i)->tm.init = d.init;
    ram_decl_at(st,i)->tm.period = d.timeout;
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
    res_param_t r;
    tstr_t frame;        // the #buffer this field is a view into
    ivalue_t bit0;
    ivalue_t bit1;
    decl_opts_t opts;
} can_param_t;

static const uint8_t pat_can[] = {
    P_STR, csp_offsetof(timer_param_t, name),
    P_PAT, PAT_RES, csp_offsetof(can_param_t, r), STOP_CAN_RES_CONT,
    P_OPTS, csp_offsetof(can_param_t, opts),
    P_STR, csp_offsetof(can_param_t, frame),
    P_TOK, LB,
    P_CHOICE, 2,
      // note the longer pattern first !!!
      P_ALT, 11,
        P_INTEGER_S, csp_offsetof(can_param_t, bit0), STOP_CAN_BIT0,
	P_TOK, DOT, P_TOK, DOT,
        P_INTEGER_S, csp_offsetof(can_param_t, bit1), STOP_CAN_BIT1,
      P_ALT_END,
      P_ALT, 4,
        P_INTEGER_S, csp_offsetof(can_param_t, bit0), STOP_CAN_BIT00,
      P_ALT_END,
    P_CHOICE_END,
    P_TOK, RB,
    P_END
};

NOINLINE int csp_parse_can(csp_rt_t* st, token_t* tv, int ti, size_t n)
{
    can_param_t d = {0};
    index_t ix, idx;
    int i, len;

    d.bit0 = d.bit1 = -1;
    d.r.res = 1;  // single bit is default ok?
    if (pmatch(st, tv, ti, n, pat_can, &d, sizeof(d)) < 0) {
	csp_set_error(st, ERR_SYNTAX);
	return -1;
    }
    // The frame must already be declared: a field is a view into it, and the
    // buffer carries the id, the size and the direction.
    if ((idx = csp_lookup_decl(st, &d.frame)) == BAD_INDEX) {
	csp_set_error(st, ERR_VARIABLE_NOT_DECLARED);
	return -1;
    }
    if ((decl(st, INDEX(idx), type) != DECL_BUFFER) ||
	(decl(st, INDEX(idx), bf.transport) != TR_CAN)) {
	csp_set_error(st, ERR_NOT_A_MODULE);   // "not a can buffer"
	return -1;
    }
    if ((ix = csp_new_udecl(st, &d.name, DECL_CAN)) == BAD_INDEX)
	return -1;
    if ((d.bit0 >= 0) && (d.bit1 >= d.bit0))
	len = (d.bit1 - d.bit0)+1;
    else if ((d.r.res > 0) && (d.bit0 >= 0))
	len = d.r.res;
    else {
	csp_set_error(st, ERR_SYNTAX);
	return -1;
    }

    i = INDEX(ix);
    ram_decl_at(st,i)->res = MAKE_RES(d.r.res); // same as len?
    ram_decl_at(st,i)->vt = d.opts.vt;
    ram_decl_at(st,i)->dir = d.opts.dir;
    ram_decl_at(st,i)->ca.id = INDEX(idx);   // the #buffer decl
    ram_decl_at(st,i)->ca.bit = d.bit0;
    ram_decl_at(st,i)->ca.len = MAKE_CAN_LEN(len);
    ram_decl_at(st,i)->ca.endian = d.opts.endian;
    // The field inherits its direction from the frame unless it says otherwise:
    // a frame is read or written as a whole, so per-field dir is rarely wanted.
    if (ram_decl_at(st,i)->dir == 0)
	ram_decl_at(st,i)->dir = decl(st, INDEX(idx), dir);
    return 0;
}

// '#' 'buffer' <name> ':' <size> [<opt>*] ['can' <frame-id>]
// Heap-backed storage. Used directly like a variable (whole buffer = value);
// later mapped with bit-field views (#variable X:n bind Buf[a..b], #can).
//
// The size unit follows the transport: BITS for a plain buffer, BYTES for a
// CAN frame -- a frame's size is its DLC, and writing `:64` for a classic
// 8-byte frame would read as CAN FD to anyone who knows the bus.
//   #buffer Buf:16                 16 bits
//   #buffer F201:8  in  can 0x201  8 bytes, classic frame
//   #buffer Fbig:64 out can 0x300  64 bytes, CAN FD
typedef struct {
    tstr_t name;
    res_param_t r;
    decl_opts_t opts;
    ivalue_t frameid;
    uint8_t is_can;
} buffer_param_t;

static const uint8_t pat_buffer[] = {
    P_STR, csp_offsetof(buffer_param_t, name),
    P_PAT, PAT_RES, csp_offsetof(buffer_param_t, r), STOP_BUFFER_RES_CONT,
    P_OPTS, csp_offsetof(buffer_param_t, opts),
    P_OPT, 5, P_TOK, T_CAN,
      P_INTEGER_S, csp_offsetof(buffer_param_t, frameid), STOP_BUFFER_CAN_ID,
    P_OPT_END,
    P_END
};

NOINLINE int csp_parse_buffer(csp_rt_t* st, token_t* tv, int ti, size_t n)
{
    buffer_param_t d = {0};
    index_t ix;
    uint32_t nbits;
    int i;

    d.r.res = 8;                 // default one byte
    d.frameid = -1;              // no 'can' clause seen
    d.opts.vt = V_UNSIGNED;      // raw bits -> unsigned by default
    if (pmatch(st, tv, ti, n, pat_buffer, &d, sizeof(d)) < 0) {
	csp_set_error(st, ERR_SYNTAX);
	return -1;
    }
    d.is_can = (d.frameid >= 0);
    nbits = d.is_can ? (uint32_t)d.r.res * 8 : (uint32_t)d.r.res;
    if ((nbits == 0) || (nbits > 1023) || (d.is_can && (d.r.res > 64))) {
	csp_set_error(st, ERR_SYNTAX);
	return -1;
    }
    if ((ix = csp_new_udecl(st, &d.name, DECL_BUFFER)) == BAD_INDEX)
	return -1;
    i = INDEX(ix);
    ram_decl_at(st,i)->vt  = d.opts.vt;
    ram_decl_at(st,i)->dir = d.opts.dir;
    ram_decl_at(st,i)->bf.nbits = nbits;
    ram_decl_at(st,i)->bf.transport = d.is_can ? TR_CAN : TR_NONE;
    if (d.is_can) {
	value_t fid;
	index_t cx;
	fid.i = d.frameid;
	if ((cx = lookup_const(st, V_INTEGER, fid)) == BAD_INDEX)
	    cx = new_signed_const(st, fid.i);
	ram_decl_at(st,i)->bf.id = cx;
    }
    return 0;
}

//
// lookup lhs in assignement
// var =  oix '.' fld
//      | obj '.' fld
//      | fld
//      | <var> '[' <pos> ']'             // one bit
//      | <var> '[' <pos0> .. <pos1> ']'  // start pos / end pos
//
// FIXME: add part 
//      <var> '.' <part>   part = 'port'|'pin'|'period'...
//
NOINLINE index_t lookup_lhs(csp_rt_t* st, const token_t* tv,
			    index_t oix, const pexpr_t* lhs)
{
    index_t ix;
    const tstr_t* name;
    index_t mx;
    ivalue_t dn;
    index_t jx;

    if (oix == BAD_INDEX) {
	if (lhs->len == 1) {  // global | module local
	    name = &tv[lhs->pos].v.str;
	    if ((ix = csp_lookup_decl(st,name)) == BAD_INDEX)
		return BAD_INDEX;
	    // Only a member of THIS module is per-instance; a global resolved
	    // from inside the body stays global.
	    if (is_module_local(st, ix))
		ix = MAKE_INDEX(CURRENT, INDEX(ix));
	}
	else if (lhs->len == 3) {  // obj.field
	    name = &tv[lhs->pos].v.str;
	    if (((oix = csp_lookup_decl(st,name)) == BAD_INDEX) ||
		(decl(st,INDEX(oix),type) != DECL_OBJECT)) {
		if (csp_set_error(st, ERR_OBJECT_NOT_DECLARED)) {
		    csp_set_err_arg_tstr(st, 0, name);
		}
		return BAD_INDEX;
	    }
	    name = &tv[lhs->pos+2].v.str;
	    goto field;
	}
	else if ((lhs->len >= 4) && (tv[lhs->pos+1].t == LB)) {
	    // Buf[pos] / Buf[pos0..pos1] -- byte access on a buffer
	    int j = lhs->pos;
	    ivalue_t p0, p1;
	    name = &tv[j].v.str;
	    if ((ix = csp_lookup_decl(st, name)) == BAD_INDEX) {
		if (csp_set_error(st, ERR_VARIABLE_NOT_DECLARED))
		    csp_set_err_arg_tstr(st, 0, name);
		return BAD_INDEX;
	    }
	    if (decl(st,INDEX(ix),type) != DECL_BUFFER) {
		csp_set_error(st, ERR_SYNTAX);
		return BAD_INDEX;
	    }
	    if (tv[j+2].t != INT) { csp_set_error(st, ERR_SYNTAX); return BAD_INDEX; }
	    p0 = p1 = tv[j+2].v.val.i;
	    if ((tv[j+3].t == DOT) && (tv[j+4].t == DOT)) {
		if (tv[j+5].t != INT) {csp_set_error(st,ERR_SYNTAX);return BAD_INDEX;}
		p1 = tv[j+5].v.val.i;
	    }
	    return make_buf_view(st, ix, p0*8, (p1+1)*8-1);
	}
	else {
	    csp_set_error(st, ERR_SYNTAX);
	    return BAD_INDEX;
	}
    }
    else if (lhs->len == 1) {  // oix. <field>
	name = &tv[lhs->pos].v.str;
	goto field;
    }
    else {
	csp_set_error(st, ERR_SYNTAX);
	return BAD_INDEX;
    }
    return ix;
field:
    mx = decl(st,INDEX(oix),mq.mx);  // module def
    dn = decl(st,INDEX(mx),md.n);  // number of elements	    
    if ((jx = lookup_decl_in(st, name,
			     INDEX(mx)+1,INDEX(mx)+1+dn))==BAD_INDEX) {
	if (csp_set_error(st, ERR_FIELD_NOT_FOUND)) {
	    csp_set_err_arg_tstr(st, 0, name);
	}
	return BAD_INDEX;
    }
    ix = MAKE_INDEX(decl(st,INDEX(oix),mq.m),INDEX(jx));
    return ix;
}


#define MAX_BODY_PARTS 8

// one rule body part: [<obj>['.'<fld>]['['<idx0>['..'<idx1>]']'] (=|<-)] <rhs>
typedef struct {
    tstr_t  obj;
    tstr_t  fld;
    tstr_t  pfld;   // third dotted name: <obj>.<fld>.<part>
    int assign;     // NONE / EQ / RIMP
    int has_idx;    // nonzero (= LB token) if '[' index present
    int has_range;  // nonzero (= DOT token) if '..' range present
    int idx_bits;   // idx0/idx1 are bit positions (pack), not byte indices
    int is_unpack;  // <var> = <src_view>  (unpack: rhs is a prebuilt sub-view)
    index_t src_view; // the source sub-view decl (when is_unpack)
    ivalue_t idx0;  // Buf[idx0 ..]
    ivalue_t idx1;  // Buf[.. idx1]
    pexpr_t rhs;
} rule_body_part_t;

//  part (',' part)* [? cond]
//
//     lhs   op   rhs    ? cond
//  -------------------------
//  print(x)             ? x > 10
//  obj.x    =  10       ? (y > z)
//  x        <- y+z      ? (y < z)
//  x = 1, y = 2         ? (y < z)
//  print(x)            [? true]
//
NOINLINE int asm_rule(csp_rt_t* st, const token_t* tv, size_t n,
		      index_t oix, const rule_body_part_t* part, int np,
		      const pexpr_t* cond)
{
    size_t num;
    int j, k;
    int dst = -1;
    int cnd = -1;
    int cnd2 = -1;

#ifdef DEBUG
    DBG("asm_rule\n");
    DBG("oix = %d\n", oix);
    DBG("np=%d\n", np);
    for (k = 0; k < np; k++) {
	DBG("part[%d]: assign=%d, rhs.len=%d, rhs.pos=%d\n", k,
	    part[k].assign, part[k].rhs.len, part[k].rhs.pos);
    }
    if (cond)
	DBG("cond.len=%d, cond.pos=%d\n", cond->len, cond->pos);
#endif
    if (st->sdef >= 0) {  // we are in a state!
	int sr;
	cnd = alloc_reg(st);
	
	if (st->sdef <= TINY_MAX)
	    asm_EQI(st, cnd, st->sx, st->sdef);
	else {
	    if (!asm_mem(st,OP_LD,cnd,st->sx)) // load state into cnd
		return -1;
	    sr = alloc_reg(st);
	    if (!asm_LI(st, sr, st->sdef))
		return -1;
	    if (!asm_EQEQ(st, cnd, cnd, sr))
		return -1;
	    free_reg(st, sr);
	}
    }

    // dry run (get nvar) union over all <- parts
    st->nvar = 0;
    for (k = 0; k < np; k++) {
	if (part[k].assign == RIMP) {
	    int r;
	    num = part[k].rhs.len;
	    st->rimp = 1;
	    r = csp_parse_const_expr(st, &tv[part[k].rhs.pos], &num, NULL);
	    st->rimp = 0;
	    if (r == 0)
		return -1;
	}
    }
    if (st->nvar) {
	cnd2 = alloc_reg(st);
	if (!asm_LI(st, cnd2, 0))
	    return -1;
	for (k = 0; k < st->nvar; k++) {
	    if (!asm_mem(st, OP_CHG, cnd2, st->var[k]))
		return -1;
	}
    }
    // cnd = state condition
    // cnd2 = changed condition
    if ((cnd >= 0) && (cnd2 >= 0)) {
	if (!asm_AND(st, cnd, cnd, cnd2))
	    return -1;
	free_reg(st, cnd2);
    }
    else if (cnd2 >= 0)
	cnd = cnd2;

    if (cond && ((num = cond->len) > 0)) {
	rentry_t rcond;
	// generate condition
	if (!csp_parse_expr(st, &tv[cond->pos], &num, &rcond))
	    return -1;
	if (!rcond.L) csp_load(st, &rcond);
	if (cnd < 0) {
	    cnd = alloc_reg(st);
	    if (!asm_MOV(st, cnd, rcond.reg))
		return -1;
	}
	else {
	    if (!asm_AND(st, cnd, rcond.reg, cnd))
		return -1;
	}
	free_reg(st, rcond.reg);
    }
    if (cnd < 0) {
	cnd = alloc_reg(st);
	if (!asm_LI(st, cnd, -1))
	    return -1;
    }
    if (!asm_RULE(st, &j, cnd, 0))
	return -1;
    free_reg(st, cnd);
    for (k = 0; k < np; k++) {
	rentry_t rbody;
	index_t ix = BAD_INDEX;
	csp_part_t lpart = PART_VAL;
	pexpr_t lhs;

	if (part[k].is_unpack) {          // <var> = <buffer>[bits]   (unpack)
	    index_t lx = csp_lookup_decl(st, &part[k].obj);
	    if (lx == BAD_INDEX) {
		csp_set_error(st, ERR_SYNTAX);
		return -1;
	    }
	    if ((OBJ(lx) == 0) && is_module_local(st, lx))
		lx = MAKE_INDEX(CURRENT, INDEX(lx));
	    if (dst >= 0) { free_reg(st, dst); dst = -1; }
	    memset(&rbody, 0, sizeof(rbody));
	    rbody.ix = part[k].src_view;  // load from the source sub-view
	    rbody.X  = 1;
	    rbody.vt = decl(st,INDEX(part[k].src_view),vt);
	    if (!coerce_assign(st, lx, &rbody))
		return -1;
	    if (!rbody.L)
		csp_load(st, &rbody);
	    dst = rbody.reg;
	    if (!asm_mem(st, OP_ST, dst, lx))
		return -1;
	    continue;
	}

	if ((num = part[k].rhs.len) == 0)
	    continue;
	if (dst >= 0) {  // only last part value is kept (for NEXT)
	    free_reg(st, dst);
	    dst = -1;
	}
	if (!csp_parse_expr(st, &tv[part[k].rhs.pos], &num, &rbody))
	    return -1;
	// lhs tokens directly precede the assign token
	lhs.pos = 0;
	lhs.len = 0;
	if (part[k].assign != 0) {
	    if (part[k].has_idx) {            // <buf> '[' i0 ['..' i1] ']' op <rhs>
		index_t bx = csp_lookup_decl(st, &part[k].obj);
		ivalue_t lo, hi;
		decl_t bt = (bx==BAD_INDEX)?DECL_NONE:decl(st,INDEX(bx),type);
		// byte access targets a buffer; pack (idx_bits) also a variable
		if ((bx == BAD_INDEX) || ((bt != DECL_BUFFER) &&
		    !(part[k].idx_bits && (bt == DECL_VARIABLE)))) {
		    csp_set_error(st, ERR_SYNTAX);
		    return -1;
		}
		if (part[k].idx_bits) {       // already bit positions
		    lo = part[k].idx0;
		    hi = part[k].idx1;
		}
		else {                        // byte index -> bit range
		    ivalue_t p0 = part[k].idx0;
		    ivalue_t p1 = part[k].has_range ? part[k].idx1 : p0;
		    lo = p0*8;
		    hi = (p1+1)*8-1;
		}
		if ((ix = make_buf_view(st, bx, lo, hi)) == BAD_INDEX)
		    return -1;
		if (!coerce_assign(st, ix, &rbody))
		    return -1;
	    }
	    else if (part[k].pfld.len > 0) {  // <obj> '.' <fld> '.' <part>
		csp_part_t pt = part_from_tstr(&part[k].pfld);
		pexpr_t of;
		if (pt == PART_LAST) {
		    csp_set_error(st, ERR_SYNTAX);
		    return -1;
		}
		of.pos = part[k].rhs.pos - 6;  // <obj> '.' <fld>
		of.len = 3;
		if ((ix = lookup_lhs(st, tv, oix, &of)) == BAD_INDEX)
		    return -1;
		lpart = pt;
	    }
	    else if (part[k].fld.len > 0) {   // <obj> '.' <fld>  (field or part)
		csp_part_t pt = part_from_tstr(&part[k].fld);
		index_t ox2 = csp_lookup_decl(st, &part[k].obj);
		int is_obj = (ox2 != BAD_INDEX) &&
		    (decl(st,INDEX(ox2),type) == DECL_OBJECT);
		if (!is_obj && (pt != PART_LAST)) { // <var|field> '.' <part>
		    if (oix != BAD_INDEX) {   // object-init: obj is a field
			pexpr_t f;
			f.pos = part[k].rhs.pos - 4;
			f.len = 1;
			if ((ix = lookup_lhs(st, tv, oix, &f)) == BAD_INDEX)
			    return -1;
		    }
		    else {                    // global | module-local var
			ix = ox2;
			if (ix == BAD_INDEX) {
			    if (csp_set_error(st, ERR_VARIABLE_NOT_DECLARED))
				csp_set_err_arg_tstr(st, 0, &part[k].obj);
			    return -1;
			}
			if ((OBJ(ix) == 0) && is_module_local(st, ix))
			    ix = MAKE_INDEX(CURRENT, INDEX(ix));
		    }
		    lpart = pt;
		}
		else {                        // <obj> '.' <fld> op <rhs>
		    lhs.pos = part[k].rhs.pos - 4;
		    lhs.len = 3;
		}
	    }
	    else if (part[k].obj.len > 0) {   // <var> op <rhs>
		lhs.pos = part[k].rhs.pos - 2;
		lhs.len = 1;
	    }
	}
	if (lhs.len > 0) {
	    if ((ix = lookup_lhs(st, tv, oix, &lhs)) == BAD_INDEX)
		return -1;
	    if (!coerce_assign(st, ix, &rbody))
		return -1;
	}
	// <var> = <small-int-imm>  (plain value, not reactive) -> single STI,
	// mirroring EQI so the rule lists cleanly (State=OFF, not State=3). dst is
	// a dead register: STI never writes it, but NEXT points at it so the body
	// renders as the assignment.
	if ((ix != BAD_INDEX) && (lpart == PART_VAL) && !rbody.L &&
	    fits_sti((part[k].assign == RIMP) ? OP_STIMP : OP_ST, &rbody)) {
	    dst = alloc_reg(st);
	    if (!asm_STI(st, dst, ix, (int8_t)rbody.val.i))
		return -1;
	    continue;
	}
	if (!rbody.L)
	    csp_load(st, &rbody);
	dst = rbody.reg;
	if (ix != BAD_INDEX) {
	    if (lpart != PART_VAL) {      // <var> '.' <part> op <rhs>  -> STP
		if (!asm_mem_part(st, OP_STP, dst, ix, lpart))
		    return -1;
	    }
	    else {
		opcode_t op = (part[k].assign == RIMP) ? OP_STIMP : OP_ST;
		if (!asm_mem(st, op, dst, ix))
		    return -1;
	    }
	}
    }
    if (dst < 0) { // no body value, load TRUE
	dst = alloc_reg(st);
	if (!asm_LI(st, dst, 0))
	    return -1;
    }
    ram_instr_at(st,j)->r.nxt = st->ps.nn - j;  // relative offset
    if (!asm_NEXT(st, dst))
	return -1;
    free_reg(st, dst);
    return 0;
}

#define MAX_INITS 8

typedef struct {
    tstr_t mod_name;
    tstr_t obj_name;
    rule_body_part_t inits[MAX_INITS];
} object_param_t;

// '#' ModName ObjName (Field (=|<-) Expr)*
static const uint8_t pat_object[] = {
    P_STR, csp_offsetof(object_param_t, mod_name),
    P_STR, csp_offsetof(object_param_t, obj_name),
    P_OPT, 11,
    P_REP, 8,
      P_ARRAY, csp_offsetof(object_param_t, inits), sizeof(rule_body_part_t),
      P_PAT, PAT_BODY, 0, STOP_OBJECT_INIT_CONT,
    P_REP_END,
    P_OPT_END,    
    P_END
};

// Emit an always-true rule "state_ix = snum" (RULE/body/NEXT shape, like
// asm_rule). Used for the object-init INIT auto-transition to NORMAL.
NOINLINE static int asm_state_set(csp_rt_t* st, index_t state_ix, int snum)
{
    reg_t cnd, dst;
    int rpos;

    cnd = alloc_reg(st);
    if (!asm_LI(st, cnd, -1))            // always-true condition
	return -1;
    if (!asm_RULE(st, &rpos, cnd, 0))
	return -1;
    free_reg(st, cnd);
    dst = alloc_reg(st);                 // dead reg for NEXT's body value
    if (!asm_STI(st, dst, state_ix, snum))
	return -1;
    ram_instr_at(st, rpos)->r.nxt = st->ps.nn - rpos;  // skip body when false
    if (!asm_NEXT(st, dst))
	return -1;
    free_reg(st, dst);
    return 0;
}

NOINLINE int csp_parse_object(csp_rt_t* st, token_t* tv, int ti, size_t n)
{
    object_param_t d = {0};
    index_t mx, ix;
    int m, k;
    
    // Parse: # ModName ObjName (Field (=|<-) Expr)*
    if (pmatch(st, tv, ti, n, pat_object, &d, sizeof(d)) < 0) {
	csp_set_error(st, ERR_SYNTAX);
	return -1;
    }

    // Lookup module (global)
    if ((mx = lookup_decl_in(st, &d.mod_name, 0, st->ps.nd)) == BAD_INDEX) {
	if (csp_set_error(st, ERR_MODULE_NOT_DECLARED)) {
	    csp_set_err_arg_tstr(st, 0, &d.mod_name);
	}
	return -1;
    }
    if (decl(st,INDEX(mx),type) != DECL_MODULE) {
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
    ram_decl_at(st, INDEX(ix))->mq.mx = mx;
    m = st->ps.nq + 1;
    ram_decl_at(st, INDEX(ix))->mq.m = m;
    st->object[m] = ix;
    st->ps.nq++;

    DBG("object %s.%s\n", decl_name(st, mx), decl_name(st, ix));    

    // Generate code for the init list. Two kinds:
    //  - reactive (<-) bindings become STANDING rules (must re-evaluate every
    //    cycle as their inputs change).
    //  - static (=, .part=) config/initial values run ONCE: gated on the
    //    object's State == INIT and terminated by State = NORMAL. Writing them
    //    every cycle is wasteful and, for config parts (OP_STP), keeps `anyd`
    //    set forever so the program never idles. The State = NORMAL is a default
    //    the module's own #in INIT may override within the same cycle (its write
    //    lands in DOUT after this one, and last-writer wins at commit).
    {
	// obj.State: State is always the module's first member (module decl + 1).
	index_t state_ix = MAKE_INDEX(m, INDEX(mx) + 1);
	int nstatic = 0;
	int mk = -1;
	reg_t cnd;

	// Emit reactive (<-) bindings as standing rules; compact the static parts
	// to the front of d.inits[] (in place, order preserved) so they can go
	// into ONE grouped rule below.
	for (k = 0; (k < MAX_INITS) && (d.inits[k].obj.len > 0); k++) {
	    if (d.inits[k].assign == RIMP) {          // reactive: standing rule
		rule_body_part_t p = d.inits[k];
		if (asm_rule(st, tv, n, ix, &p, 1, NULL) < 0)
		    return -1;
	    }
	    else {
		if (nstatic != k)
		    d.inits[nstatic] = d.inits[k];
		nstatic++;
	    }
	}

	if (nstatic > 0) {
	    cnd = alloc_reg(st);
	    if (!asm_mem(st, OP_LD, cnd, state_ix))   // load obj.State
		return -1;
	    if (!asm_INSTATE(st, &mk, cnd, 0))        // gate on INIT (snum 0)
		return -1;
	    free_reg(st, cnd);
	    // all static config/init writes in ONE rule (shares one LI/RULE/NEXT)
	    if (asm_rule(st, tv, n, ix, d.inits, nstatic, NULL) < 0)
		return -1;
	    // auto-transition, AFTER the last static init: State = NORMAL (snum 1)
	    if (asm_state_set(st, state_ix, 1) < 0)
		return -1;
	    // patch the gate to skip the whole INIT block when State != INIT
	    ram_instr_at(st, mk)->in.nxt = st->ps.nn - mk;
	}
    }
    return asm_NEW(st, decl(st,INDEX(mx),md.ent), ix);
}

//
// Parse rule
//
//  <lhs> = <rhs> [ ? <cond> ]
//  <lhs> <- <rhs> [ ? <cond> ]
//  <lhs> [ ? <cond> ]
//
//  <lhs> = <var> | <obj>'.'<var>
//  <rhs> = <expr>
//  <cond> = <expr>
//

// cond first! body parts must start at a byte offset
typedef struct {
    pexpr_t cond;
    rule_body_part_t body[MAX_BODY_PARTS];
} rule_param_t;

// PAT_BODY: [<name> ['.' <name> ['.' <name>]] ['[' <idx0> ['..' <idx1>] ']'] (=|<-)] <expr>
static const uint8_t pat_body[] = {
    P_OPT, 54,
      P_STR, csp_offsetof(rule_body_part_t, obj),
      P_OPT, 12,
        P_TOK, DOT,
        P_STR, csp_offsetof(rule_body_part_t, fld),
        P_OPT, 5,
          P_TOK, DOT,
          P_STR, csp_offsetof(rule_body_part_t, pfld),
        P_OPT_END,
      P_OPT_END,
      P_OPT, 20,
        P_TOK_W, LB, csp_offsetof(rule_body_part_t, has_idx),
        P_INTEGER_S, csp_offsetof(rule_body_part_t, idx0), STOP_BODY_IDX0,
        P_OPT, 9,
          P_TOK_W, DOT, csp_offsetof(rule_body_part_t, has_range),
          P_TOK, DOT,
          P_INTEGER_S, csp_offsetof(rule_body_part_t, idx1), STOP_BODY_IDX1,
        P_OPT_END,
        P_TOK, RB,
      P_OPT_END,
      P_CHOICE, 2,
        P_ALT, 4, P_TOK_W, EQ, csp_offsetof(rule_body_part_t, assign), P_ALT_END,
        P_ALT, 4, P_TOK_W, RIMP, csp_offsetof(rule_body_part_t, assign), P_ALT_END,
      P_CHOICE_END,
    P_OPT_END,
    P_EXPR_S, csp_offsetof(rule_body_part_t, rhs), STOP_RULE_BODY,
    P_END
};

// <rule> := <body> (',' <body>)* ['?' <cond>]
static const uint8_t pat_rule[] = {
    P_PAT, PAT_BODY, csp_offsetof(rule_param_t, body), STOP_RULE_BODY_CONT,
    P_REP, 10,
      P_ARRAY, csp_offsetof(rule_param_t, body[1]), sizeof(rule_body_part_t),
      P_TOK, COMMA,
      P_PAT, PAT_BODY, 0, STOP_RULE_BODY_CONT,
    P_REP_END,
    P_OPT, 6,
      P_TOK, QUEST,
      P_EXPR_S, csp_offsetof(rule_param_t, cond), STOP_RULE_COND,
    P_OPT_END,
    P_END
};

NOINLINE int csp_parse_rule(csp_rt_t* st, const token_t* tv, int ti, size_t n)
{
    rule_param_t d = {0};
    int np = 0;

    if (pmatch(st, tv, ti, n, pat_rule, &d, sizeof(d)) < 0) {
	csp_set_error(st, ERR_SYNTAX);
	return -1;
    }
    while ((np < MAX_BODY_PARTS) && (d.body[np].rhs.len > 0))
	np++;
    return asm_rule(st, tv, n, BAD_INDEX, d.body, np, &d.cond);
}

// '<buffer>' '<<=' <field>... ['?' <cond>]   (frame packing)
//   <field> := <expr> [':' <bits>]   blank separated
// Fields are laid out at ascending bit offsets, each masked to its width.
// Sugar for a sequence of bit-field stores: <buf>[off..off+w-1] = <expr>.
#define MAX_PACK MAX_BODY_PARTS

typedef struct {
    pexpr_t  val;     // value expression to pack
    ivalue_t bits;    // field width in bits; 0 => the value's declared width
} pack_field_t;

typedef struct {
    pack_field_t field[MAX_PACK];
    tstr_t       buffer;
    int          op;       // LTLT = pack '<<=', GTGT = unpack '>>='
    pexpr_t      cond;
} pack_param_t;

static const uint8_t pat_field[] = {
    P_EXPR_S, csp_offsetof(pack_field_t, val), STOP_PACK_VAL,
    P_OPT, 6,
      P_TOK, COLON,
      P_INTEGER_S, csp_offsetof(pack_field_t, bits), STOP_PACK_BITS,
    P_OPT_END,
    P_END
};

static const uint8_t pat_pack[] = {
    P_STR, csp_offsetof(pack_param_t, buffer),
    P_CHOICE, 2,
      P_ALT, 4, P_TOK_W, LTLT, csp_offsetof(pack_param_t, op), P_ALT_END,
      P_ALT, 4, P_TOK_W, GTGT, csp_offsetof(pack_param_t, op), P_ALT_END,
    P_CHOICE_END,
    P_TOK, EQ,
    P_REP, 8,
      P_ARRAY, csp_offsetof(pack_param_t, field), sizeof(pack_field_t),
      P_PAT, PAT_FIELD, 0, STOP_PACK_FIELD_CONT,
    P_REP_END,
    P_OPT, 6,
      P_TOK, QUEST,
      P_EXPR_S, csp_offsetof(pack_param_t, cond), STOP_RULE_COND,
    P_OPT_END,
    P_END
};

NOINLINE int csp_parse_pack(csp_rt_t* st, token_t* tv, size_t n)
{
    pack_param_t d = {0};
    rule_body_part_t part[MAX_PACK];
    index_t bx;
    int np = 0, off = 0;
    decl_t bt;
    int unpack;
    
    if (pmatch(st, tv, 0, n, pat_pack, &d, sizeof(d)) < 0) {
	csp_set_error(st, ERR_SYNTAX);
	return -1;
    }
    unpack = (d.op == GTGT);
    bx = csp_lookup_decl(st, &d.buffer);
    bt = (bx == BAD_INDEX) ? DECL_NONE : decl(st,INDEX(bx),type);
    if ((bt != DECL_BUFFER) && (bt != DECL_VARIABLE)) {
	csp_set_error(st, ERR_SYNTAX);
	return -1;
    }
    while ((np < MAX_PACK) && (d.field[np].val.len > 0)) {
	pack_field_t* f = &d.field[np];
	int w = f->bits;
	index_t fx = BAD_INDEX;            // the field's variable, if a single name
	if ((f->val.len == 1) && (tv[f->val.pos].t == WORD))
	    fx = csp_lookup_decl(st, &tv[f->val.pos].v.str);
	if (w <= 0) {                      // default width: variable's resolution
	    if (fx == BAD_INDEX) {
		csp_set_error(st, ERR_SYNTAX);
		return -1;
	    }
	    w = GET_RES(decl(st,INDEX(fx),res));
	}
	memset(&part[np], 0, sizeof(part[np]));
	part[np].assign = EQ;
	if (unpack) {                      // <var> = <buffer>[off..off+w-1]
	    index_t vw;
	    if (fx == BAD_INDEX) {         // unpack target must be a variable
		csp_set_error(st, ERR_SYNTAX);
		return -1;
	    }
	    if ((vw = make_buf_view(st, bx, off, off + w - 1)) == BAD_INDEX)
		return -1;
	    part[np].obj       = tv[f->val.pos].v.str;
	    part[np].is_unpack = 1;
	    part[np].src_view  = vw;
	}
	else {                             // <buffer>[off..off+w-1] = <expr>
	    part[np].obj      = d.buffer;
	    part[np].has_idx  = 1;
	    part[np].idx_bits = 1;
	    part[np].idx0     = off;
	    part[np].idx1     = off + w - 1;
	    part[np].rhs      = f->val;
	}
	off += w;
	np++;
    }
    return asm_rule(st, tv, n, BAD_INDEX, part, np,
		    (d.cond.len > 0) ? &d.cond : NULL);
}

// create (or reuse) a synthetic HEAP sub-view into buffer `parent`, covering
// bits [b0 .. b1]. Used for Buf[pos]/Buf[pos0..pos1]. Parent decl index, start
// bit and length are stashed in the can fields and translated to a VIEW_HEAP
// entry in csp_rt_start (after the parent buffer has been allocated).
NOINLINE index_t make_buf_view(csp_rt_t* st, index_t parent,
			       ivalue_t b0, ivalue_t b1)
{
    index_t ix;
    int i;
    index_t pi = INDEX(parent);
    const tstr_t name = { .ptr = NULL, .len = 0 };

    for (i = 0; i < st->ps.nd; i++) {  // dedup
	if ((ram_decl_at(st,i)->type == DECL_VIEW) &&
	    (ram_decl_at(st,i)->ca.id == pi) &&
	    (ram_decl_at(st,i)->ca.bit == b0) &&
	    (ram_decl_at(st,i)->ca.len == MAKE_CAN_LEN((b1-b0)+1)))
	    return MAKE_INDEX(0, i);
    }
    if ((ix = csp_new_decl(st, &name, DECL_VIEW)) == BAD_INDEX)
	return BAD_INDEX;
    i = INDEX(ix);
    ram_decl_at(st,i)->vt     = V_UNSIGNED;
    ram_decl_at(st,i)->dir    = decl(st,pi,dir);
    ram_decl_at(st,i)->res    = MAKE_RES((b1-b0)+1);
    ram_decl_at(st,i)->ca.id  = pi;
    ram_decl_at(st,i)->ca.bit = b0;
    ram_decl_at(st,i)->ca.len = MAKE_CAN_LEN((b1-b0)+1);
    ram_decl_at(st,i)->ca.endian = E_NATIVE;
    return ix;
}

// '>' command
NOINLINE int csp_parse_immediate(csp_rt_t* st, token_t* tv, int ti, size_t n)
{
    return 0;
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
	DBG("added state %d %.*s\n", s, csp_str_byte(st,i-1), csp_str_at(st,i));
#endif
	return s;
    }
    csp_set_error(st, ERR_TOO_MANY_STATES);
    return -1;
}

NOINLINE int csp_parse_in(csp_rt_t* st, token_t* tv, int ti, size_t n)
{
    int i;
    if (tv[ti].t != WORD) return -1;

    if (st->sdef != -1) {
	csp_set_error(st, ERR_END_MISMATCH);
	return -1;
    }
    if ((i = lookup_state(st, &tv[ti].v.str)) < 0) {
	csp_set_err_arg_tstr(st, 0, &tv[2].v.str);
	csp_set_error(st, ERR_STATE_NOT_DECLARED);
	return -1;
    }
    // compile time state, add rules to states[i].snum
    st->sdef = st->states[i].snum;
    // Emit the block gate: LD the current State into a scratch register, then
    // OP_INSTATE which skips the whole block (sequential) when State != sdef.
    // nxt is patched at #end. The per-rule EQI stays for the reactive path.
    {
	int mk;
	reg_t cnd = alloc_reg(st);
	if (!asm_mem(st, OP_LD, cnd, st->sx))
	    return -1;
	if (!asm_INSTATE(st, &mk, cnd, st->sdef))
	    return -1;
	free_reg(st, cnd);
	st->in_marker = mk;
    }
    return 0;
}

// no need to use pattern parser here, yet too simple
NOINLINE int csp_parse_states(csp_rt_t* st, token_t* tv, int ti, size_t n)
{
    int i;
    for (i = ti; i < n; i++) {
	int j;
	if (tv[i].t != WORD) return -1;
	if ((j = lookup_state(st, &tv[i].v.str)) >= 0)
	    continue; // already installed (no error maybe warning?)
	if (add_state(st, &tv[i].v.str) < 0)
	    return -1;
    }
    return 0;
}

NOINLINE int csp_parse(csp_rt_t* st, char* str)
{
    token_t tv[MAX_LINE_TOKENS];
    size_t num = MAX_LINE_TOKENS;
    reg_allocator_t alloc;
    int n;

    st->ap = &alloc;
    while((n = csp_scan_line(st, str, tv, &num)) > 0) {
	int r = -1;
	str += n;
	alloc_init(st->ap);

	st->ps.line++;

	if (tv[0].t == NEWLINE)
	    r = 0;
	else if ((tv[0].t == HASH) && (tv[1].t == IN)) {
	    r = csp_parse_in(st, tv, 2, num);
	}
	// 'can' is a reserved token (the transport option on #buffer), so it does
	// not arrive here as a WORD -- same reason #in needs its own branch.
	else if ((tv[0].t == HASH) && (tv[1].t == T_CAN)) {
	    r = csp_parse_can(st, tv, 2, num);
	}
	else if ((tv[0].t == HASH) && (tv[1].t == WORD)) {
	    int i;
	    if ((i = find_decl_entry(tv[1].v.str.ptr,tv[1].v.str.len)) >= 0) {
		switch(decl_table[i].code) {
		case DECL_MODULE:
		    r = csp_parse_module(st, tv, 2, num);
		    break;
		case DECL_STATES:  // '#' 'states' WORD ... WORD
		    r = csp_parse_states(st, tv, 2, num);
		    break;
		case DECL_END:
		    r = csp_parse_end(st, tv, 2, num);
		    break;
		case DECL_VARIABLE:
		    r = csp_parse_variable(st, tv, 2, num);
		    break;
		case DECL_CONSTANT:
		    r = csp_parse_constant(st, tv, 2, num);
		    break;
		case DECL_DIGITAL:
		    r = csp_parse_digital(st, tv, 2, num);
		    break;
		case DECL_ANALOG:
		    r = csp_parse_analog(st, tv, 2, num);
		    break;
		case DECL_TIMER:
		    r = csp_parse_timer(st, tv, 2, num);
		    break;
		case DECL_CAN:
		    r = csp_parse_can(st, tv, 2, num);
		    break;
		case DECL_BUFFER:
		    r = csp_parse_buffer(st, tv, 2, num);
		    break;
		default:
		    r = -1;
		    csp_set_error(st, ERR_SYNTAX);
		    break;
		}
	    }
	    else {
		r = csp_parse_object(st, tv, 1, num);
	    }
	}
	else if (tv[0].t == GT) {
	    r = csp_parse_immediate(st, tv, 1, num);
	}
	else if ((num >= 3) && (tv[0].t == WORD) && (tv[2].t == EQ) &&
		 ((tv[1].t == LTLT) || (tv[1].t == GTGT))) {
	    r = csp_parse_pack(st, tv, num);        // <buf> <<=/>>= <fields>
	}
	else {
	    r = csp_parse_rule(st, tv, 0, num);
	}
	if (r < 0)
	    return -1;
	num = MAX_LINE_TOKENS;
    }
    return n;
}

// True when firmware with executable rules is linked in (rom.c).
int csp_has_firmware(void)
{
    return rom_n_instr > 0;
}

// Activate the linked firmware ROM: run it in place from flash by setting the
// RAM base offsets to the ROM sizes. No copy -- csp_get_decl/instr read flash
// for logical indices below the base. The parse_file DECL_END terminator at the
// end of the ROM image is dropped so RAM decls append seamlessly.
// STEG2: not called yet (base stays 0, ROM inactive); needs State-from-ROM.
NOINLINE void csp_load_rom(csp_rt_t* st)
{
    index_t nd = rom_n_decl;
    if (rom_n_decl == 0)          // no firmware linked
	return;
    st->rom_decl_p  = rom_decl;
    st->rom_instr_p = rom_instr;
    st->rom_str_p   = rom_str;
    if ((nd > 0) && (ro_decl(&rom_decl[nd-1]).type == DECL_END))
	nd--;                     // drop the trailing terminator
    st->rom_nd   = nd;
    st->rom_nn   = rom_n_instr;
    st->rom_strp = rom_str_len;
    // Rebase the parse state onto ROM: RAM starts empty above the ROM sizes.
    // This discards the RAM State/strings csp_rt_init created -- State is now
    // ROM decl 0, and the ROM string prefix mirrors init's so the states table
    // (INIT/NORMAL name offsets) still resolves correctly through the flash.
    st->ps.nd   = st->rom_nd;
    st->ps.nn   = st->rom_nn;
    st->ps.strp = st->rom_strp;
    st->sx = 0;                   // State is ROM decl 0
    // Restore the baked state table (name offsets index rom_str, which we just
    // pointed at). Overwrites the INIT/NORMAL seed from csp_rt_init with the
    // program's full table so ON/OFF/... resolve in listing and lookup.
    {
	int i, ns = rom_n_states;
	if (ns > MAX_STATES) ns = MAX_STATES;
	for (i = 0; i < ns; i++)
	    st->states[i] = ro_state(&rom_states[i]);
	st->ps.ns = ns;
	st->rom_ns = ns;   // baseline; EEPROM persists only additions above this
    }
}

// Backend hook: hand out the raw RAM for the code arena. The memory source is a
// per-target choice; the layout below is shared. Default is a static buffer so a
// new backend (LPCopen, bare-metal, no-heap) works with no porting. A weak
// symbol would be cleaner but gets stripped under -ffunction-sections/gc-sections
// on some targets (see the weak-ROM note), so select at build time instead.
// `want` is how much the runtime would like to claim; *got is set to what the
// backend can actually hand over (a claim may cap out, a static buffer is fixed).
#if defined(CSP_ARENA_CUSTOM)
// backend provides its own csp_arena_mem() (e.g. sbrk-based claim of free RAM)
#elif defined(CSP_ARENA_MALLOC)
uint8_t* csp_arena_mem(size_t want, size_t* got)   // claim: take what we asked for
{
    uint8_t* p = (uint8_t*)malloc(want);
    *got = p ? want : 0;
    return p;
}
#else
uint8_t* csp_arena_mem(size_t want, size_t* got)   // portable default: no heap
{
    static uint8_t arena[CSP_ARENA_BYTES] __attribute__((aligned(8)));
    *got = (want < sizeof(arena)) ? want : sizeof(arena);  // fixed ceiling
    return arena;
}
#endif

// Lay out the RAM code arena as two forward-indexed regions: instr[] (MAX_INSTRS
// +1 slots -- the last is the immediate-eval scratch slot) followed by decl[].
// `size` is reserved for a future single shared pool; step 1 uses the fixed
// layout that exactly matches the old static arrays. The raw memory comes from
// the csp_arena_mem() backend hook. Returns 0 on success, -1 on failure.
// Struct size the RAM model should charge. On a target it is the real thing; on
// the host --board overrides it with the target's (16/32-bit) size so the model
// is not skewed by the host's wider pointers.
size_t csp_sim_state = 0;
static uint32_t model_state(void)
{
    return (uint32_t)(csp_sim_state ? csp_sim_state : sizeof(csp_rt_t));
}

int csp_mem_init(csp_rt_t* st, size_t size)
{
    size_t want;
    // How much to CLAIM. With no explicit budget, take everything the system left
    // us less the stack we still grow into -- csp_system_ram_avail() already
    // excludes the system (freeRam() on a target: what is free after the core and
    // every linked library took theirs), so nothing about the board's overhead is
    // guessed. This one pool holds the program AND its derived data, so it is
    // CandySpeak's whole RAM budget, fixed at boot from what is actually free
    // rather than a per-board constant. `size` (linux -m) overrides, for testing a
    // tighter budget than the machine really has.
    if (size > 0)
	want = size;
    else {
	uint32_t avail = csp_system_ram_avail();
	uint32_t over  = model_state() + CSP_STACK_RESERVE;
	want = (avail > over) ? (avail - over) : 0;
    }
    want = CSP_A8(want);

    // The backend hands back what it can actually give (a claim may cap out, a
    // static buffer is fixed). The whole block is the pool; CSP_CODE_BUDGET is no
    // longer the ceiling -- the claim is.
    st->mem = csp_arena_mem(want, &st->mem_size);
    if ((st->mem == NULL) || (st->mem_size < 16))
	return -1;
    st->mem_size &= ~(size_t)7;          // keep the decl anchor 8-aligned
    st->mem_limit = st->mem_size;
    memset(st->mem, 0, st->mem_size);

    // Double-ended pool: instr[] grows up from the base, decl[] grows down from
    // the top of the CLAIMED block. ram_decl points at the topmost decl slot and
    // is indexed with a negated local index (ram_decl_at / csp_get_decl). Base and
    // top are both 8-aligned. They can never overlap: mem_fits keeps instr+decl,
    // and the middle bump allocator keeps the derived tables, inside mem_limit.
    st->ram_instr = (csp_instr_t*)st->mem;
    st->ram_decl  = (csp_decl_t*)(st->mem + st->mem_limit) - 1;
    // view/dset/buf/heap are all sized to the estimate in csp_rt_start.
    st->view = NULL;
    st->dset = NULL;
    st->view_cap = 0;
    st->heap[DIN] = st->heap[DOUT] = NULL;
    st->buf = NULL;
    st->buf_cap = 0;
    st->heap_cap = 0;
    // input/output/timer are sized to the estimate in csp_rt_start too.
    st->input = NULL;  st->in_cap = 0;
    st->output = NULL; st->out_cap = 0;
    st->timer = NULL;  st->timer_cap = 0;
#if defined(SUPPORT_REACTIVE) && (SUPPORT_REACTIVE==1)
    // pending sets, rule_ip and idg/ofs/edg are all sized to actual in csp_csr.
    st->pending[0] = st->pending[1] = NULL;
    st->pending_cap = 0;
    st->obj_shift = 0;
    st->gen = 0;
    st->rule_ip = NULL;
    st->n_rule = 0;
    st->idg = st->ofs = st->edg = NULL;
#endif
    return 0;
}

int csp_rt_init(csp_rt_t* st, int reactive)
{
    memset(st, 0x00, sizeof(csp_rt_t));

    // Allocate the RAM code arena (instr[]/decl[]) before anything parses.
    if (csp_mem_init(st, 0) < 0)
	return -1;

    // Initialize stop-sets for P_EXPR_S patterns
    init_stop_sets();       // creates STOP_NONE (index 0)

    scan_pattern(PAT_PORT_PIN, pat_port_pin);
    scan_pattern(PAT_RES,      pat_res);   

    scan_pattern(PAT_MODULE,   pat_module);
    scan_pattern(PAT_END,      pat_end);
    scan_pattern(PAT_VARIABLE, pat_variable);
    scan_pattern(PAT_CONSTANT, pat_constant);
    scan_pattern(PAT_DIGITAL,  pat_digital);
    scan_pattern(PAT_ANALOG,   pat_analog);
    scan_pattern(PAT_TIMER,    pat_timer);
    scan_pattern(PAT_CAN,      pat_can);
    scan_pattern(PAT_BUFFER,   pat_buffer);
    scan_pattern(PAT_BODY,     pat_body);
    scan_pattern(PAT_RULE,     pat_rule);
    scan_pattern(PAT_FIELD,    pat_field);
    scan_pattern(PAT_PACK,     pat_pack);
    scan_pattern(PAT_OBJECT,   pat_object);
    
#ifdef DEBUG
    if (debug) // or precompile!
	dump_stop_sets();       // debugging
#endif
    st->nbuf = 0;   // heap[]/tables already point into the arena (csp_mem_init)
    st->reactive = reactive;
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
    st->n_rule_emit = 1;   // the implicit rule body at the RAM range base
    st->var = st->var_buf;  // dedicated scratch for var list during <- parse
    st->nvar = 0;
    st->rimp = 0;

    st->ram_str[0] = 0;  // reserved 0 and nil
    st->ufuncs = NULL;
    st->num_ufuncs = 0;
    st->uconst = NULL;
    {
	const tstr_t State = { .ptr = "State", .len = 5};
	const tstr_t INIT  = { .ptr = "INIT", .len = 4};
	const tstr_t NORMAL = { .ptr = "NORMAL", .len = 6};
	st->ps.ns = 0;  // install INIT (cycle()==0) and NORMAL
	st->sx = csp_new_decl(st, &State, DECL_VARIABLE);
	st->sdef = -1;
	// add state INIT=0 and NORMAL=1
	if (add_state(st, &INIT) != 0) {
	    DBG("unabled to add INIT state=0\n");
	    return -1;
	}
	if (add_state(st, &NORMAL) != 1) {
	    DBG("unabled to add NORMAL state=1\n");
	    return -1;
	}
	st->rom_ns = st->ps.ns;  // baseline (2); raised by csp_load_rom if firmware
    }
    st->list_state = -1;         // no #in block being listed
    return 0;
}

// Set user function table (called before parsing)
// rom = 1 if the funcs table (and, per-entry FUNC_RONAME, its names) is in ROM.
void csp_set_ufuncs(csp_rt_t* st, const csp_func_t* funcs, uint8_t count, uint8_t rom)
{
    st->ufuncs = funcs;
    st->num_ufuncs = count;
    st->ufuncs_rom = rom;
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
    csp_decl_t d = csp_get_decl(st, INDEX(ix));  // ROM or RAM decl, by value

    // clear timeout flag and load config into the timer's value_t buffer.
    // The start-time slot (tx = ix+1) is an ordinary variable, initialised to 0
    // by its own setup_variable; the timer arms it at runtime.
    csp_dio_slots(st, ix, &iptr, &optr);
    iptr->t.fired = optr->t.fired = 0;
    iptr->t.val = optr->t.val = d.tm.init;
    iptr->t.running = optr->t.running = d.tm.init;
    iptr->t.period = optr->t.period = d.tm.period;
}


// copy config data to value slot config
NOINLINE static void setup_analog(csp_rt_t* st, index_t ix)
{
    value_t* iptr;
    value_t* optr;
    csp_decl_t d = csp_get_decl(st, INDEX(ix));

    csp_dio_slots(st, ix, &iptr, &optr);
    iptr->a.dir  = optr->a.dir     = d.dir;
    iptr->a.pin  = optr->a.pin     = d.an.pin;
    iptr->a.port = optr->a.port    = d.an.port;
    iptr->a.pwm  = optr->a.pwm     = d.an.pwm;
    iptr->a.endian = optr->a.endian = d.an.endian;
}

// copy config data to value slot config
NOINLINE static void setup_digital(csp_rt_t* st, index_t ix)
{
    value_t* iptr;
    value_t* optr;
    csp_decl_t d = csp_get_decl(st, INDEX(ix));

    csp_dio_slots(st, ix, &iptr, &optr);
    iptr->d.dir  = optr->d.dir = d.dir;
    iptr->d.pin  = optr->d.pin = d.di.pin;
    iptr->d.port = optr->d.port = d.di.port;
    iptr->d.pullup = optr->d.pullup = d.di.pullup;
    iptr->d.pulldown = optr->d.pulldown = d.di.pulldown;
}

NOINLINE static index_t csp_buf_alloc(csp_rt_t* st, uint8_t nbytes,
				      uint8_t transport, uint32_t xref,
				      pindir_t dir);
NOINLINE static int parent_leaf(csp_rt_t* st, index_t ix);

// Bind a #can field to its frame. ca.id is the #buffer decl; that buffer was
// already allocated by setup_buffer (it has a lower decl index, since the frame
// must be declared before a field can view it), so this is purely a view.
NOINLINE static int setup_can(csp_rt_t* st, index_t ix)
{
    int i = INDEX(ix);
    index_t fx = decl(st, i, ca.id);            // the #buffer (decl index)
    uint16_t pos = decl(st, i, ca.bit);         // ca.bit is 9 bits: 0..511
    csp_view_t* pv = &st->view[parent_leaf(st, ix)];
    csp_view_t* vw;

    // The declaration can name any bit of a 64-byte FD frame, but csp_view_t.pos
    // is a byte -- so only the first 32 bytes are addressable at runtime. Refuse
    // rather than silently wrap to a wrong field.
    if (pos > 255) {
	csp_set_error(st, ERR_TOO_MANY_DECLARATIONS);
	return -1;
    }
    if (pos + decl(st,i,ca.len) + 1 > decl(st, fx, bf.nbits)) {
	csp_set_error(st, ERR_SYNTAX);          // field reaches past the frame
	return -1;
    }
    vw = &st->view[st_index(st, ix)];
    vw->kind   = VIEW_HEAP;
    vw->vt     = decl(st, i, vt);
    vw->buf    = pv->buf;                       // share the frame's buffer
    vw->pos    = (uint8_t)pos;
    vw->len    = decl(st, i, ca.len);
    vw->endian = decl(st, i, ca.endian);
    vw->flags  = 0;
    return 0;
}

// ============================================================
// CAN frame I/O
//
// A frame is one buffer; its fields are bit views into it. So receiving is a
// memcpy into the committed (DIN) heap and sending is a memcpy out of it --
// the packing and unpacking is the view machinery, already there.
//
// This is where the CANopen PDO shapes fall out, without any PDO code:
//   RPDO       frame arrives -> fields change -> dependent rules enqueue
//   TPDO event a rule writes a field -> commit marks dirty -> sent
//   TPDO cyclic  Frame.X = ... ? timeout(T)  -- the timer is the trigger
// ============================================================

// A received frame has to enter through the same door as any other device
// input: compare against the committed value, and where a field differs mark
// it dirty and push its dependents. That is what makes changed() see it and
// what wakes the reactive rules.
//
// No extra copy of the old bytes is needed -- the transaction already keeps
// one. csp_can_input drops the frame into the DOUT shadow, DIN still holds the
// previous frame, so the diff below is old-vs-new, and commit then moves DOUT
// to DIN exactly as it does for a value a rule wrote.
NOINLINE static void can_mark_fields(csp_rt_t* st, index_t b)
{
    int i;
    // The frame's own leaf first. A frame declared as a plain #buffer (no #can
    // fields at all -- read with >>= or with bound variables) has nothing in
    // the input list, so without this nothing would be marked, commit would
    // copy nothing, and the received bytes would never reach the committed
    // half. heap_dset_copy moves the WHOLE buffer for any dirty leaf of it, so
    // this one mark is what actually lands the frame.
    for (i = 0; i < (int)st->ps.nd; i++) {
	if ((decl(st,i,type) != DECL_BUFFER) ||
	    (st->view[i].buf != b))
	    continue;
	// Marked unconditionally, not diffed: a frame is wider than a value_t,
	// so there is no single value to compare. Granularity comes from the
	// per-field pass below; this mark only has to make the copy happen.
	bitset_set(st->dset, i);
	st->anyd = CSP_TRUE;
#if defined(SUPPORT_REACTIVE) && (SUPPORT_REACTIVE==1)
	if (st->reactive)
	    csp_enq_elist(st, MAKE_INDEX(0, i));
#endif
	break;
    }
    // Then per-field, so changed() has real granularity and only the fields
    // that moved wake their rules.
    for (i = 0; i < st->ni; i++) {
	index_t ix = st->input[i];
	int leaf;
	csp_view_t* vw;
	if (decl(st, INDEX(ix), type) != DECL_CAN)
	    continue;
	leaf = st_index(st, ix);
	vw = &st->view[leaf];
	if (vw->buf != b)
	    continue;
	if (csp_heap_get(st, vw, DOUT).u == csp_heap_get(st, vw, DIN).u)
	    continue;                     // this field of the frame is unchanged
	bitset_set(st->dset, leaf);
	st->anyd = CSP_TRUE;
#if defined(SUPPORT_REACTIVE) && (SUPPORT_REACTIVE==1)
	if (st->reactive)
	    csp_enq_elist(st, ix);
#endif
	st->update++;
    }
}

// Does this program listen to the bus? A driver loop has to keep running for a
// program whose only input is CAN -- there is no timer and nothing changes, so
// every other "is there work left" test says no and the loop would quit before
// the first frame ever arrived.
int csp_can_active(csp_rt_t* st)
{
    index_t b;
    for (b = 0; b < st->nbuf; b++) {
	if ((st->buf[b].transport == TR_CAN) && (st->buf[b].dir & DIR_IN))
	    return 1;
    }
    return 0;
}

// Drain the receive side. One frame can feed several buffers only if the same
// id is declared twice, so the loop below keeps scanning rather than stopping
// at the first match.
void csp_can_input(csp_rt_t* st)
{
    uint8_t data[64];
    uint32_t id;
    uint8_t len;
    int guard;

    // Bounded: a busy bus would otherwise keep this loop fed forever and the
    // cycle would never run.
    for (guard = 0; guard < CSP_CAN_RX_BURST; guard++) {
	index_t b;
	if (csp_can_recv(st, &id, data, &len) != 1)
	    return;
	for (b = 0; b < st->nbuf; b++) {
	    csp_buf_t* bp = &st->buf[b];
	    uint8_t n;
	    if ((bp->transport != TR_CAN) || (bp->xref != id) ||
		!(bp->dir & DIR_IN))
		continue;
	    // Into the SHADOW, not the committed half: DIN must keep the previous
	    // frame so can_mark_fields can tell what actually changed.
	    // A short frame updates only the bytes it carried. Clamp per buffer,
	    // not once: the same id may feed several buffers of different sizes.
	    n = (len < bp->nbytes) ? len : bp->nbytes;
	    memcpy(st->heap[DOUT] + bp->hp, data, n);
	    bp->dlc = n;                   // what the sender actually sent
	    bp->flags |= BUF_F_RXPEND;     // csp_commit turns this into BUF_F_RX
	    can_mark_fields(st, b);
	}
    }
}

// Send every out frame that either changed or was explicitly asked for.
// BUF_F_TX is what makes a CYCLIC PDO expressible: an unchanged frame is not
// dirty, so `F.tx = 1 ? timeout(T)` is the only way to say "send it anyway".
void csp_can_output(csp_rt_t* st)
{
    index_t b;
    for (b = 0; b < st->nbuf; b++) {
	csp_buf_t* bp = &st->buf[b];
	if ((bp->transport != TR_CAN) || !(bp->flags & (BUF_F_DIRTY|BUF_F_TX)))
	    continue;
	bp->flags &= ~(BUF_F_DIRTY|BUF_F_TX);
	if (bp->dir & DIR_OUT)
	    csp_can_send(st, bp->xref, st->heap[DIN] + bp->hp, bp->dlc);
    }
}

// bump-allocate a buffer in the heap, return its id (or BAD_INDEX)
NOINLINE static index_t csp_buf_alloc(csp_rt_t* st, uint8_t nbytes,
				      uint8_t transport, uint32_t xref,
				      pindir_t dir)
{
    index_t b = st->nbuf;
    uint16_t hp = (b == 0) ? 0 : (st->buf[b-1].hp + st->buf[b-1].nbytes);
    hp = (hp + 3) & ~3;   // 4-align so value_t access into the heap is aligned
    // Table and heap are sized to csp_estimate; overrun means the estimate was
    // short -- fail loudly (see the driver's rt_start check) instead of scribbling.
    if ((b >= st->buf_cap) || (hp + nbytes > st->heap_cap)) {
	csp_set_error(st, ERR_TOO_MANY_DECLARATIONS);
	return BAD_INDEX;
    }
    st->buf[b].hp        = hp;
    st->buf[b].nbytes    = nbytes;
    st->buf[b].loc       = 0;          // RAM
    st->buf[b].transport = transport;
    st->buf[b].xref      = xref;
    st->buf[b].dir       = dir;
    st->buf[b].flags     = 0;
    st->buf[b].dlc       = nbytes;     // send the whole frame unless told less
    st->nbuf++;
    return b;
}

// allocate storage for a #buffer and point its own view at the whole buffer
NOINLINE static int setup_buffer(csp_rt_t* st, index_t ix)
{
    int i = INDEX(ix);
    // Only a real #buffer carries bf.nbits. This function is shared with the
    // auto-buffer a plain #variable gets, and there the size lives in res.
    int is_buf = (decl(st,i,type) == DECL_BUFFER);
    uint16_t res = is_buf ? decl(st,i,bf.nbits) : GET_RES(decl(st,i,res));
    uint8_t nbytes = (res + 7) >> 3;
    uint8_t transport = is_buf ? decl(st,i,bf.transport) : TR_NONE;
    uint32_t xref = 0;
    index_t b;
    csp_view_t* vw;

    if (transport == TR_CAN)                 // the frame id, out of its constant
	xref = (uint32_t)decl(st, decl(st,i,bf.id), cn.init).i;
    if ((b = csp_buf_alloc(st, nbytes, transport, xref, decl(st,i,dir)))
	== BAD_INDEX)
	return -1;
    vw = &st->view[st_index(st, ix)];
    vw->kind     = VIEW_HEAP;
    vw->vt       = decl(st,i,vt);
    vw->buf    = b;
    vw->pos    = 0;
    // A whole-frame view would need len up to 511, but len is 8 bits. Cap it:
    // the frame is read and written field by field, and the whole-buffer view
    // only matters for a plain #buffer used as one value.
    vw->len    = (res > 256) ? 255 : (uint8_t)(res - 1);
    vw->endian = E_NATIVE;
    vw->flags  = ((res & 7) == 0) ? VIEW_F_SIMPLE : 0;
    return 0;
}

// A variable lives in the heap: bound -> a view into an existing buffer,
// otherwise its own auto-buffer seeded with the init value. Works for globals
// and per-object fields alike (st_index/decl pick the right slot/template).
// Leaf of the buffer that decl `ix` is a view into. ca.id holds the parent's
// DECL index, which is not a leaf: for a member of an object, a parent that is
// a member of the SAME module lives in that object's storage, while a global
// parent does not. Getting this wrong aliases every instance onto the module
// template's slot.
NOINLINE static int parent_leaf(csp_rt_t* st, index_t ix)
{
    index_t p = decl(st, INDEX(ix), ca.id);
    int m = OBJ(ix);
    if ((m != 0) && (m != CURRENT)) {
	index_t ox = st->object[m];
	index_t mx = decl(st, INDEX(ox), mq.mx);
	int base = INDEX(mx) + 1;
	int dn = decl(st, INDEX(mx), md.n);
	if (((int)p >= base) && ((int)p < base + dn))
	    return st_index(st, MAKE_INDEX(m, p));   // this instance's parent
    }
    return st_index(st, MAKE_INDEX(0, p));           // a global parent
}

NOINLINE static int setup_variable(csp_rt_t* st, index_t ix)
{
    int i = INDEX(ix);
    csp_view_t* vw = &st->view[st_index(st, ix)];

    if (decl(st,i,bound)) {                   // bit-field view into a buffer
	csp_view_t* pv = &st->view[parent_leaf(st, ix)];
	vw->kind     = VIEW_HEAP;
	vw->vt       = decl(st,i,vt);
	vw->buf    = pv->buf;
	vw->pos    = decl(st,i,ca.bit);
	vw->len    = decl(st,i,ca.len);
	vw->endian = decl(st,i,ca.endian);
	vw->flags  = 0;
	return 0;
    }
    if (setup_buffer(st, ix) < 0)         // auto-buffer
	return -1;
    csp_heap_set(st, vw, DIN,  decl(st,i,va.init));
    csp_heap_set(st, vw, DOUT, decl(st,i,va.init));
    return 0;
}

// config+value types (constant/digital/analog/timer) live as a value_t struct
// in their own buffer. Allocate it + point a VIEW_SLOT view at it; the caller
// then fills it through the normal csp_dio_slot(s)/PART path (now -> heap).
NOINLINE static int setup_slot(csp_rt_t* st, index_t ix)
{
    int i = INDEX(ix);
    index_t b = csp_buf_alloc(st, sizeof(value_t), 0, 0, decl(st,i,dir));
    csp_view_t* vw;
    if (b == BAD_INDEX)
	return -1;
    vw = &st->view[st_index(st, ix)];
    vw->kind = VIEW_SLOT;
    vw->vt   = decl(st,i,vt);
    vw->buf  = b;
    return 0;
}

NOINLINE static void add_io(csp_rt_t* st, index_t ix)
{
    int i = INDEX(ix);
    if (decl(st,i,dir) & DIR_IN) {
	if (st->ni < st->in_cap)  // sized to csp_estimate.ni; guard is belt+braces
	    st->input[st->ni++] = ix;
    }
    if (decl(st,i,dir) & DIR_OUT) {
	if (st->no < st->out_cap)
	    st->output[st->no++] = ix;
    }
}

// Count the buffer/heap/io a single value-leaf decl `j` needs, mirroring the
// setup_* functions (which decls get a buffer, its byte size, and whether it is
// device I/O). Used by csp_estimate; must track those functions to stay exact.
NOINLINE static void est_leaf(csp_rt_t* st, int j, csp_estimate_t* e)
{
    uint32_t nbytes;
    switch (decl(st,j,type)) {
    case DECL_VARIABLE:
	if (decl(st,j,bound))                 // bit-field view: shares a buffer
	    return;
	nbytes = (GET_RES(decl(st,j,res)) + 7) >> 3;
	break;
    case DECL_BUFFER:
	nbytes = (decl(st,j,bf.nbits) + 7) >> 3;   // #buffer: size is in bf.nbits
	break;
    case DECL_TIMER:
	e->nt++;                              // timer list entry (setup_timer)
	nbytes = sizeof(value_t);
	break;
    case DECL_CONSTANT:
	nbytes = sizeof(value_t);
	break;
    case DECL_DIGITAL:
    case DECL_ANALOG:
	nbytes = sizeof(value_t);
	if (decl(st,j,dir) & DIR_IN)  e->ni++;
	if (decl(st,j,dir) & DIR_OUT) e->no++;
	break;
    case DECL_CAN:
	// A field is a view into its #buffer, which is counted as a buffer in
	// its own right. Only the I/O list entry belongs here.
	if (decl(st,j,dir) & DIR_IN)  e->ni++;
	if (decl(st,j,dir) & DIR_OUT) e->no++;
	return;
    default:
	return;                               // module/end/object/view: no buffer
    }
    e->nbuf++;
    e->heap += (nbytes + 3) & ~3u;            // 4-aligned like csp_buf_alloc
}

// Memory an already-parsed program needs, WITHOUT running csp_rt_start: the same
// global + per-object walk (offs build included), counting only. nleaf must be
// exact (it sizes view[]/dset); nbuf/heap/ni/no match the setup_* allocations.
void csp_estimate(csp_rt_t* st, csp_estimate_t* e)
{
    int i, in_module = 0;
    index_t nd = st->ps.nd;
    int offs = nd;                            // object storage base (== rt_start)

    e->nleaf = nd;                            // globals occupy leaves [0, nd)
    e->nbuf = e->ni = e->no = e->nt = 0;
    e->heap = 0;

    for (i = 0; i < (int)nd; i++) {           // globals
	switch (decl(st,i,type)) {
	case DECL_MODULE: in_module = 1; break;
	case DECL_END:    in_module = 0; break;
	case DECL_OBJECT: break;                  // handled in the object pass
	default:
	    if (!in_module) est_leaf(st, i, e);
	    break;
	}
    }
    for (i = 0; i < (int)nd; i++) {           // objects: offs build + members
	index_t mx;
	int dn, base, j, top;
	if (decl(st,i,type) != DECL_OBJECT)
	    continue;
	mx = decl(st, i, mq.mx);
	dn = decl(st, INDEX(mx), md.n);
	base = INDEX(mx) + 1;                  // members' decl indices: base..base+dn-1
	top = offs + base + dn;               // one past this object's last leaf
	if (top > (int)e->nleaf) e->nleaf = top;
	for (j = 0; j < dn; j++)
	    est_leaf(st, base + j, e);
	offs += dn;
    }
}

// Lay the whole program out again: the reactive graph, then the leaf/device
// setup. These two used to be called separately, which was fine while each
// malloc'd its own tables -- but they now bump-allocate from one shared middle
// region, so running rt_start alone would hand csr's tables out a second time.
// One entry point keeps the region consistent, and it also means a declaration
// added at the prompt rewires the graph (running rt_start alone left the new
// decl above graph_n, i.e. with no edges, so it never fired reactively).
int csp_rebuild(csp_rt_t* st)
{
    csp_mid_reset(st);          // forget the old layout; everything below re-bumps
#if defined(SUPPORT_REACTIVE) && (SUPPORT_REACTIVE==1)
    if (st->reactive)
	csp_csr(st);
#endif
    // Everything emitted so far is now covered: clear both staleness signals.
    st->graph_rules = st->n_rule_emit;
    st->edited = 0;
    return csp_rt_start(st);
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
    csp_estimate_t e;

    // Size every derived table to what this program actually declares
    // (csp_estimate, no prior setup) -- nothing here is a MAX_* reservation.
    // Freed+reallocated on every rebuild; the fill below stays within these.
    // rt_start reruns on any decl add (see csp_process_persistent) so view_cap
    // always covers max st_index.
    {
	size_t hbytes;
	csp_estimate(st, &e);
	// The DIN/DOUT transaction halves are ONE block: heap[DOUT] points at the
	// second half. 8-aligned so a buffer at the same hp offset is equally
	// aligned in both halves.
	hbytes = CSP_A8(e.heap ? e.heap : 8);
	// All of these come out of the middle, already zeroed. Nothing to free: the
	// next csp_rebuild resets the cursor and lays the region out again.
	st->view  = (csp_view_t*)csp_mid_alloc(st, (size_t)e.nleaf * sizeof(csp_view_t));
	st->dset  = (set_group_t*)csp_mid_alloc(st,
		     (size_t)BITSET_GROUPS(e.nleaf ? e.nleaf : 1) * sizeof(set_group_t));
	st->buf   = (csp_buf_t*)csp_mid_alloc(st, (size_t)e.nbuf * sizeof(csp_buf_t));
	st->heap[DIN] = (uint8_t*)csp_mid_alloc(st, 2 * hbytes);
	st->input  = (index_t*)csp_mid_alloc(st, (size_t)e.ni * sizeof(index_t));
	st->output = (index_t*)csp_mid_alloc(st, (size_t)e.no * sizeof(index_t));
	st->timer  = (index_t*)csp_mid_alloc(st, (size_t)e.nt * sizeof(index_t));
	if (!st->view || !st->dset || !st->buf || !st->heap[DIN] ||
	    !st->input || !st->output || !st->timer) {
	    st->heap[DOUT] = NULL;
	    st->view_cap = 0; st->buf_cap = 0; st->heap_cap = 0;
	    st->in_cap = 0; st->out_cap = 0; st->timer_cap = 0;
	    csp_set_error(st, ERR_TOO_MANY_DECLARATIONS);
	    return -1;
	}
	st->heap[DOUT] = st->heap[DIN] + hbytes;   // second half of the same block
	st->view_cap = e.nleaf;
	st->buf_cap  = e.nbuf;
	st->heap_cap = e.heap;
	st->in_cap   = e.ni;
	st->out_cap  = e.no;
	st->timer_cap = e.nt;
    }

#if defined(SUPPORT_REACTIVE) && (SUPPORT_REACTIVE==1)
    if (st->pending_cap) {          // drop any work left from a previous build
	uint32_t g = BITSET_GROUPS(st->pending_cap) * sizeof(set_group_t);
	memset(st->pending[0], 0, g);
	memset(st->pending[1], 0, g);
	st->gen = 0;
    }
#endif
    // SAFE state: unwind any runtime accumulators so /clear, /reset and a
    // post-parse rebuild all land on a coherent baseline (like boot's memset).
    st->esp = 0;              // drop half-run OP_NEW/OP_LEAVE call stack
    st->cur = 0;              // back to the global module
    memset(st->dset, 0, BITSET_GROUPS(st->view_cap) * sizeof(set_group_t));    // no stale dirty leaves survive into the rebuild
    st->nt = 0;
    st->ni = 0;
    st->no = 0;
    st->nm = 0;
    st->nbuf = 0;
    st->ps.nq = 0;   // rebuilt from DECL_OBJECT below (parse-time table is not
		     // restored from ROM); idempotent for a freshly parsed program

    // (view/dset/heap were allocated + zeroed above; setup_* fills view below.)

    for (i = 0; i < st->ps.nd; i++) {
	index_t ix = MAKE_INDEX(0,i);
	switch(decl(st,i,type)) {
	case DECL_MODULE:
	    in_module=1;
	    if (st->nm < MAX_MODULES)
		st->module[st->nm++] = ix;
	    break;
	case DECL_END:
	    in_module = 0;
	    break;
	case DECL_OBJECT:
	    // Rebuild the object slot table (1-based, decl order == parse order),
	    // so ROM-baked objects get per-object storage and list with their real
	    // names. Per-object value init still happens after offs[] is allocated.
	    if (st->ps.nq < MAX_OBJECTS-1)
		st->object[++st->ps.nq] = ix;
	    break;
	case DECL_CONSTANT:
	    if (!in_module) {               // global; templates set up per-object
		if (setup_slot(st, ix) < 0)
		    return -1;
		csp_dio_slots(st, ix, &iptr, &optr);
		*iptr = *optr = decl(st,i,cn.init);
	    }
	    break;
	case DECL_VARIABLE:
	    if (!in_module) {               // global; templates set up per-object
		if (setup_variable(st, ix) < 0)
		    return -1;
	    }
	    break;
	case DECL_TIMER:
	    if (!in_module) {
		if (setup_slot(st, ix) < 0)
		    return -1;
		setup_timer(st, ix);
		if (st->nt < st->timer_cap)
		    st->timer[st->nt++] = ix;
	    }
	    break;

	case DECL_DIGITAL:
	    if (!in_module) {
		if (setup_slot(st, ix) < 0)
		    return -1;
		setup_digital(st, ix);
		add_io(st, ix);
	    }
	    break;

	case DECL_ANALOG:
	    if (!in_module) {
		if (setup_slot(st, ix) < 0)
		    return -1;
		setup_analog(st, ix);
		add_io(st, ix);
	    }
	    break;
	    
	case DECL_CAN:
	    if (!in_module) {
		if (setup_can(st, ix) < 0)
		    return -1;
		add_io(st, ix);
	    }
	    break;
	case DECL_BUFFER:
	    if (!in_module) {
		if (setup_buffer(st, ix) < 0)
		    return -1;
	    }
	    break;
	case DECL_VIEW: {
	    // synthetic Buf[a..b] view: translate to a HEAP view into the
	    // parent buffer (already set up, since it has a lower index)
	    index_t parent = decl(st,i,ca.id);
	    csp_view_t* pv = &st->view[parent];
	    csp_view_t* vw = &st->view[st_index(st, ix)];
	    vw->kind     = VIEW_HEAP;
	    vw->vt       = decl(st,i,vt);
	    vw->buf    = pv->buf;
	    vw->pos    = decl(st,i,ca.bit);
	    vw->len    = decl(st,i,ca.len);     // already len-1
	    vw->endian = decl(st,i,ca.endian);
	    vw->flags  = 0;                       // sub-view -> generic bit path
	    break;
	}
	default:
	    break;
	}
    }
    // allocate object 1..nq storage
    offs = st->ps.nd;
    for (i = 0; i < st->ps.nq; i++) {
	int m = i+1;
	index_t ix = st->object[m];
	index_t mx = decl(st, INDEX(ix), mq.mx);  // module def
	ivalue_t dn = decl(st, INDEX(mx), md.n);  // number of decl elements
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
	index_t mx = decl(st, INDEX(ix), mq.mx); // module def
	ivalue_t dn = decl(st, INDEX(mx), md.n);  // number of decl elements

	int base = INDEX(mx)+1;
	for (j = 0; j < dn; j++) {
	    int dj = base + j;         // decl index
	    index_t fx = MAKE_INDEX(m,dj); // field index
#ifdef DEBUG
	    DBG("init OBJECT %s, FIELD %s[%d]\n",
		decl_name(st, ix), decl_name(st, fx), dj);
#endif
	    switch (decl(st, dj, type)) {
	    case DECL_CONSTANT:
		if (setup_slot(st, fx) < 0)
		    return -1;
		csp_dio_slots(st, fx, &iptr, &optr);
		*iptr = *optr = decl(st, dj, cn.init);
		break;
	    case DECL_VARIABLE:
		if (setup_variable(st, fx) < 0)
		    return -1;
		break;
	    case DECL_TIMER:
		if (setup_slot(st, fx) < 0)
		    return -1;
		setup_timer(st, fx);
		if (st->nt < st->timer_cap)
		    st->timer[st->nt++] = fx;
		break;
	    case DECL_DIGITAL:
		if (setup_slot(st, fx) < 0)
		    return -1;
		setup_digital(st, fx);
		add_io(st, fx);
		break;
	    case DECL_ANALOG:
		if (setup_slot(st, fx) < 0)
		    return -1;
		setup_analog(st, fx);
		add_io(st, fx);
		break;
	    case DECL_BUFFER:
		// Each instance gets its own storage, like every other member.
		// This case was simply missing: a #buffer inside a module parsed
		// fine and was then never allocated, and a member bound to it
		// aliased an uninitialised view.
		if (setup_buffer(st, fx) < 0)
		    return -1;
		break;
	    case DECL_CAN:
		// fx, not ix: this object's field, not the object itself. And an
		// explicit break -- it used to fall through to default.
		if (setup_can(st, fx) < 0)
		    return -1;
		add_io(st, fx);
		break;
	    default:
		break;
	    }
	}
    }
    st->cycle = 0;  // init trace shows cycle 0
    st->started = 1;   // leaves allocated + set up; value ops are now valid
    return 0;
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

#define MAX_ARGV 32

static int cmd_help(csp_rt_t* st, int argc, char* argv[]);
static int cmd_list(csp_rt_t* st, int argc, char* arg[]);
static int cmd_state(csp_rt_t* st, int argc, char* argv[]);
static int cmd_reset(csp_rt_t* st, int argc, char* argv[]);
static int cmd_clear(csp_rt_t* st, int argc, char* argv[]);
static int cmd_memory(csp_rt_t* st, int argc, char* argv[]);
static int cmd_commit(csp_rt_t* st, int argc, char* argv[]);
static int cmd_quit(csp_rt_t* st, int argc, char* argv[]);
static int cmd_latch(csp_rt_t* st, int argc, char* argv[]);
static int cmd_pause(csp_rt_t* st, int argc, char* argv[]);
static int cmd_live(csp_rt_t* st, int argc, char* argv[]);
static int cmd_resume(csp_rt_t* st, int argc, char* argv[]);

static const csp_cmd_t builtin_cmds[] = {
    { "help",   "Show this help",          cmd_help },
    { "?",      NULL,                      cmd_help },
    { "list",   "List rules",              cmd_list },
    { "state",  "Show current values",     cmd_state },
    { "memory", "Show code/RAM usage",      cmd_memory },
    { "pause",  "Stop execution (inspect/edit)", cmd_pause },
    { "live",   "Freeze rules, keep I/O (poke hardware)", cmd_live },
    { "resume", "Resume execution (rebuild if edited)", cmd_resume },
    { "reset",  "Reset to initial values", cmd_reset },
    { "clear",  "Drop RAM patches (keep ROM)", cmd_clear },
    { "latch",  "on or off, device output", cmd_latch },
    { "commit", "Commit pending values",   cmd_commit },
    { "save",   "Save state to storage",   csp_cmd_save },
    { "load",   "Load state from storage", csp_cmd_load },
    { "quit",   "Exit interactive mode",   cmd_quit },
    { "exit",   NULL,                      cmd_quit },
    { NULL, NULL, NULL }
};

static int cmd_help(csp_rt_t* st, int argc, char* argv[])
{
    (void)st; (void)argv;
    csp_print_str("Commands:\n");
    for (const csp_cmd_t* c = builtin_cmds; c->name; c++) {
	if (c->help) {
	    int len;
	    csp_print_str("  /");
	    csp_print_str(c->name);
	    len = strlen(c->name);
	    while (len++ < 10) csp_print_char(' ');
	    csp_print_str(c->help);
	    csp_print_char('\n');
	}
    }
    return CSP_CMD_OK;
}

//
// /list [filter]
//
// List rules
//
// /list LED1 LED2        write to LED1 or LED2
// /list ?A               depend on A (A is in the condition)
// /list ?A ?B            depend on A and B
// /list LED1 ?A ?B :Run  write to LED1 and depend on A and B in state Run
// /list :Fail            list rules in state Fail
//
#define MAX_FILTER 8  // MAX 32 (bitmasks)

typedef struct _filter_var_t {
    index_t ix;   // variable/constant/digital/analog/state-num
    char    typ;  // '?' in condition ' ' in body, ':' state
} filter_var_t;

// lookup filter index from var index or state number
static int lookup_filter(index_t ix, int cnd, filter_var_t* fv, int nf)
{
    int i = 0;
    while(i < nf) {
	if (ix == fv[i].ix) {
	    if ((cnd==1) && (fv[i].typ=='?'))
		return i;
	    // a bare name (typ ' ') matches wherever the var is referenced --
	    // in the body (cnd 0) OR the condition (cnd 1). '?' stays condition-only.
	    if (((cnd==0)||(cnd==1)) && (fv[i].typ==' '))
		return i;
	    if ((cnd == 2) && ((fv[i].typ=='?')||(fv[i].typ==' ')))
		return i;
	    if ((cnd == 3) && (fv[i].typ==':'))
		return i;
	}
	i++;
    }
    return -1;
}

static int is_fvar(index_t ix, int cnd, filter_var_t* fv, int nf)
{
    return (lookup_filter(ix, cnd, fv, nf) >= 0);
}

// print a leaf name (by logical string position), qualified as Mod.name when
// inside a module. mod == 0 means global. Segment-aware (ROM flash or RAM).
static void list_name(csp_rt_t* st, sindex_t mod, sindex_t name)
{
    if (mod) { csp_print_str_at(st, mod); csp_print_char('.'); }
    csp_print_str_at(st, name);
}

// find a #module declaration by name (for /list <Module> scoping)
static index_t find_module(csp_rt_t* st, const char* name)
{
    int i;
    int len = (int)strlen(name);
    for (i = 0; i < st->ps.nd; i++) {
	if (decl(st, i, type) == DECL_MODULE) {
	    if (csp_str_eq(st, decl(st,i,name), name, len))
		return MAKE_INDEX(0, i);
	}
    }
    return BAD_INDEX;
}

static int cmd_list(csp_rt_t* st, int argc, char* argv[])
{
    int i;
    index_t ix;
    int nf = 0;   // number of filters
    filter_var_t filt[MAX_FILTER];
    int f;
    uint32_t cmask;  // condition filter variables (filter index bitmask)
    uint32_t bmask;  // body filter variables (filter index bitmask)
    uint32_t fbits;  // currently present variables / states (by filter index)
    int cnd;         // in condition part
    int rule;        // rule start index (in condition part)
    const char* name;
    sindex_t cur_mod = 0;        // module name pos being listed (0 = global)
    sindex_t npos;               // current decl's name position
    const char* scope = NULL;    // restrict listing to this module (arg)

    cmask = 0;
    bmask = 0;
    // register the filter variables and types
    for (i = 0; i < argc; i++) {
	char typ = ' ';
	name = argv[i];
	if (name[0]=='?')      { typ='?'; name++; }
	else if (name[0]==':') { typ=':'; name++; }

	if (typ==':') {
	    const tstr_t sname = { (char*)name, strlen(name) };
	    int s;
	    if ((s=lookup_state(st, &sname)) >=0) {
		if ((f = lookup_filter(ix, 3, filt, nf)) < 0) {
		    if (nf >= MAX_FILTER) goto match;
		    filt[nf].typ = typ;
		    filt[nf++].ix = s;
		}
	    }
	}
	else if ((typ == ' ') && (find_module(st, name) != BAD_INDEX)) {
	    scope = name;   // restrict listing to this module's members
	}
	else {
	    const tstr_t sname = { (char*)name, strlen(name) };
	    if ((ix = csp_lookup_decl(st, &sname)) != BAD_INDEX) {
		if ((f = lookup_filter(ix, (typ == '?'), filt, nf)) < 0) {
		    if (nf >= MAX_FILTER) goto match;
		    if (typ == '?') cmask |= (1 << nf);
		    else if (typ == ' ') bmask |= (1 << nf);
		    filt[nf].typ = typ;
		    filt[nf++].ix = ix;
		}
	    }
	}
    }

match:
    // list declarations that match the filter. Iterate ALL decls (do not stop
    // at the first DECL_END -- module ends and the terminator are ENDs too).
    // Each line is tagged [ROM]/[RAM] by segment; module members are shown with
    // a Mod. prefix. scope != NULL restricts to that module's members.
    for (i = 0; i < st->ps.nd; i++) {
	index_t ix = MAKE_INDEX(0, i);
	decl_t t = decl(st,i,type);
	const char* seg = (i < st->rom_nd) ? "ROM" : "RAM";
	if (t == DECL_MODULE) {
	    cur_mod = decl(st, i, name);
	    if (!scope) {
		csp_print_str("["); csp_print_str(seg);
		csp_print_str("] #module "); csp_print_str_at(st, cur_mod);
		csp_print_char('\n');
	    }
	    continue;
	}
	if (t == DECL_END) {         // module end or top-level terminator
	    cur_mod = 0;
	    continue;
	}
	if (scope && !(cur_mod && csp_str_eq(st, cur_mod, scope, strlen(scope))))
	    continue;                // only this module's members
	npos = decl(st, i, name);
	if ((npos == 0) || (csp_str_byte(st, npos-1) == 0))
	    continue;                // no / empty name
	if (nf && !is_fvar(ix, 2, filt, nf))
	    continue;
	csp_print_str("["); csp_print_str(seg); csp_print_str("] ");
	switch (t) {
	case DECL_VARIABLE:
	    csp_print_str("#variable ");
	    list_name(st, cur_mod, npos);
	    csp_print_char(' ');
	    csp_print_str(csp_fmt_vtype(decl(st,i,vt)));
	    // list the declaration's init value, not the live state (like #constant
	    // below); reading a value here would touch leaf storage /list must not.
	    if (!decl(st,i,bound)) {
		csp_print_str(" = ");
		csp_print_value(st, decl(st,i,vt), decl(st,i,va.init));
	    }
	    else {
		// bind <buffer>[<lo>..<hi>] -- a bit-field view, so there is no
		// init value to show; without this the bind vanished from /list
		// and the output could not be pasted back.
		csp_print_str(" bind ");
		csp_print_str_at(st, decl_name_pos(st, decl(st,i,ca.id)));
		csp_print_char('[');
		csp_print_uint(decl(st,i,ca.bit));
		csp_print_str("..");
		csp_print_uint(decl(st,i,ca.bit) + decl(st,i,ca.len));
		csp_print_char(']');
	    }
	    csp_print_char('\n');
	    break;
	case DECL_CONSTANT:
	    csp_print_str("#constant ");
	    list_name(st, cur_mod, npos);
	    csp_print_char(' ');
	    csp_print_str(csp_fmt_vtype(decl(st,i,vt)));
	    csp_print_str(" = ");
	    csp_print_value(st, decl(st,i,vt), decl(st,i,cn.init));
	    csp_print_char('\n');
	    break;
	case DECL_OBJECT:
	    csp_print_str("#");
	    csp_print_str_at(st, decl_name_pos(st, decl(st,i,mq.mx)));
	    csp_print_char(' ');
	    csp_print_str_at(st, npos);
	    csp_print_char('\n');
	    break;
	case DECL_TIMER:
	    csp_print_str("#timer ");
	    list_name(st, cur_mod, npos);
	    csp_print_char('\n');
	    break;
	case DECL_DIGITAL:
	    csp_print_str("#digital ");
	    list_name(st, cur_mod, npos);
	    csp_print_char(' ');
	    csp_print_str(csp_fmt_pindir(decl(st,i,dir)));
	    csp_print_char(' ');              // port:pin (needed to mod/rewire)
	    csp_print_uint(decl(st,i,di.port));
	    csp_print_char(':');
	    csp_print_uint(decl(st,i,di.pin));
	    csp_print_char('\n');
	    break;
	case DECL_ANALOG:
	    csp_print_str("#analog ");
	    list_name(st, cur_mod, npos);
	    csp_print_char(':');              // :width (res stored as bits-1)
	    csp_print_uint(decl(st,i,an.res)+1);
	    csp_print_char(' ');
	    csp_print_str(csp_fmt_pindir(decl(st,i,dir)));
	    csp_print_char(' ');              // port:pin
	    csp_print_uint(decl(st,i,an.port));
	    csp_print_char(':');
	    csp_print_uint(decl(st,i,an.pin));
	    csp_print_char('\n');
	    break;
	case DECL_BUFFER:
	    // #buffer <name>:<size> <dir> [can 0x<id>].  Size is BYTES for a can
	    // frame (it is the DLC), bits otherwise -- see csp_parse_buffer.
	    csp_print_str("#buffer ");
	    list_name(st, cur_mod, npos);
	    csp_print_char(':');
	    if (decl(st,i,bf.transport) == TR_CAN)
		csp_print_uint(decl(st,i,bf.nbits) >> 3);
	    else
		csp_print_uint(decl(st,i,bf.nbits));
	    if (decl(st,i,dir)) {
		csp_print_char(' ');
		csp_print_str(csp_fmt_pindir(decl(st,i,dir)));
	    }
	    if (decl(st,i,bf.transport) == TR_CAN) {
		csp_print_str(" can ");   // csp_print_hex emits the 0x itself
		csp_print_hex((uvalue_t)decl(st, decl(st,i,bf.id), cn.init).i);
	    }
	    csp_print_char('\n');
	    break;
	case DECL_CAN:
	    // #can <name>:<width> <dir> <type> <frame>[<lo>..<hi>].  ca.id is the
	    // #buffer decl the field is a view into, so the frame is named, not
	    // repeated as a raw id.
	    csp_print_str("#can ");
	    list_name(st, cur_mod, npos);
	    csp_print_char(':');
	    csp_print_uint(decl(st,i,ca.len)+1);
	    csp_print_char(' ');
	    csp_print_str(csp_fmt_pindir(decl(st,i,dir)));
	    csp_print_char(' ');
	    csp_print_str(csp_fmt_vtype(decl(st,i,vt)));
	    csp_print_char(' ');
	    csp_print_str_at(st, decl_name_pos(st, decl(st,i,ca.id)));
	    csp_print_char('[');
	    csp_print_uint(decl(st,i,ca.bit));
	    csp_print_str("..");
	    csp_print_uint(decl(st,i,ca.bit) + decl(st,i,ca.len));
	    csp_print_str("]\n");
	    break;
	default:
	    csp_print_char('\n');
	    break;
	}
    }

    // now list all rules that match the filter. Each rule is tagged [ROM]/[RAM]
    // by its start index vs the ROM boundary; OP_ENTER/OP_LEAVE track the module
    // a rule belongs to (module bodies are inline but skipped during linear eval).
    rule = 0;  // condition index
    i  = rule;
    cnd = 1;
    fbits = 0;
    cur_mod = 0;
    while(i < st->ps.nn) {
	switch(instr(st,i,op)) {
	case OP_ENTER:
	    cur_mod = decl_name_pos(st, MAKE_INDEX(0, instr(st,i,e.mx)));
	    i++; rule = i;
	    break;
	case OP_LEAVE:
	    cur_mod = 0;
	    i++; rule = i;
	    break;
	case OP_RULE: cnd=0; i++;
	    break;
	case OP_NEXT:
	    cnd=1; i++;
	    {
		// Guard each mask: a zero mask must NOT vacuously pass (the old
		// `(fbits&0)==0` bug made a bare filter list every rule). Only-":"
		// state filters (cmask==bmask==0) keep the legacy show-all.
		int show = (nf==0)
		    || (cmask && ((fbits&cmask)==cmask))
		    || (bmask && ((fbits&bmask)!=0))
		    || (cmask==0 && bmask==0);
		if (scope && !(cur_mod && csp_str_eq(st, cur_mod, scope, strlen(scope))))
		    show = 0;
		if (show) {
		    csp_print_str((rule < st->rom_nn) ? "[ROM] " : "[RAM] ");
		    if (cur_mod) {
			csp_print_str_at(st, cur_mod);
			csp_print_str(": ");
		    }
		    csp_print_rule(st, rule);
		}
	    }
	    fbits = 0;
	    rule = i;  // start new rule
	    break;
	case OP_LD:
	case OP_LDP:
	    if (nf) {
		if ((f = lookup_filter(instr(st,i,m.mem), cnd, filt, nf)) >= 0)
		    fbits |= (1 << f);
	    }
	    i++;
	    break;
	case OP_STIMP:
	case OP_ST:
	case OP_STP:
	    if (nf) {
		if ((f = lookup_filter(instr(st,i,m.mem), cnd, filt, nf)) >= 0)
		    fbits |= (1 << f);
	    }
	    i++;
	    break;
	case OP_STI:  // immediate store: memory index in the .mi arm
	    if (nf) {
		if ((f = lookup_filter(instr(st,i,mi.mem), cnd, filt, nf)) >= 0)
		    fbits |= (1 << f);
	    }
	    i++;
	    break;
	default: i++; break;
	}
    }
    return CSP_CMD_OK;
}

// --- /state rendering ------------------------------------------------------
// Fixed columns: NAME  DIR  KIND  PIN  = VALUE. Only committed (DIN) values are
// shown -- the DOUT shadow is an internal half of the transaction model, not
// something a user reading their program's state should have to decode.
// The DIR column doubles as the timer's running/stopped, and the PIN column as
// its period/remaining -- a timer has neither a direction nor a pin, so it costs
// no extra columns.
#define STATE_W_NAME 13
#define STATE_W_DIR   8
#define STATE_W_KIND  9
#define STATE_W_PIN   8

static void state_pad(int printed, int w)
{
    while (printed++ < w) csp_print_char(' ');
}

static int state_strlen(const char* s)
{
    int n = 0;
    while (s && s[n]) n++;
    return n;
}

static void state_col(const char* s, int w)
{
    if (s) csp_print_str(s);
    state_pad(state_strlen(s), w);
}

static int state_udigits(uvalue_t v)
{
    int n = 1;
    while (v >= 10) { v /= 10; n++; }
    return n;
}

// Object-qualified name, as the state dump writes it: "p0.State", or the bare
// name for a global.
static void state_name(csp_rt_t* st, index_t ix)
{
    int m = OBJ(ix);
    int n = 0;
    if ((m != GLOBAL) && (m != CURRENT)) {
	const char* on = decl_name(st, st->object[m]);
	csp_print_str(on);
	csp_print_char('.');
	n = state_strlen(on) + 1;
    }
    csp_print_str(decl_name(st, ix));
    state_pad(n + state_strlen(decl_name(st, ix)), STATE_W_NAME);
}

// A DECL_VARIABLE named "State" holds a state NUMBER; show it symbolically, the
// way /list does, so "ON" does not read as "3". Same rule as the disassembler's
// is_state_var: only that name, so a plain "T=1" is never mistaken for a state.
static int state_is_state_var(csp_rt_t* st, int di)
{
    if (decl(st, di, type) != DECL_VARIABLE)
	return 0;
    return csp_str_eq(st, decl_name_pos(st, MAKE_INDEX(0, di)), "State", 5);
}

// Print the declared state numbered `v`; 0 if no state has that number.
static int state_print_state(csp_rt_t* st, ivalue_t v)
{
    int s;
    for (s = 0; s < st->ps.ns; s++) {
	if (st->states[s].snum == v) {
	    csp_print_str_at(st, st->states[s].name);
	    return 1;
	}
    }
    return 0;
}

// One leaf row. ix is the object-qualified leaf, di its declaration index.
NOINLINE static void state_row(csp_rt_t* st, index_t ix, int di)
{
    decl_t t = decl(st, di, type);
    int is_state;

    state_name(st, ix);

    if (t == DECL_TIMER) {
	value_t* v = csp_dio_slot(st, ix, DIN);
	uint32_t left = 0;
	if (v->t.running) {
	    // t0 lives in the slot right after the timer
	    index_t tx = MAKE_INDEX(OBJ(ix), INDEX(ix)+1);
	    uint32_t dt = csp_time_ms() - csp_dio_slot(st, tx, DIN)->u;
	    left = (dt >= v->t.period) ? 0 : (v->t.period - dt);
	}
	state_col(v->t.running ? "running" : "stopped", STATE_W_DIR);
	state_col("timer", STATE_W_KIND);
	csp_print_uint(v->t.period);          // period/remaining, in the pin column
	if (v->t.running) {
	    csp_print_char('/');
	    csp_print_uint(left);
	}
	if (v->t.fired)
	    csp_print_str("  FIRED");
	csp_println();
	return;
    }

    is_state = state_is_state_var(st, di);
    if (t == DECL_VARIABLE) {
	state_col("", STATE_W_DIR);
	state_col(is_state ? "state" : "var", STATE_W_KIND);
	state_col("", STATE_W_PIN);
    }
    else {   // digital / analog
	// port/pin/dir live in the VALUE slot, not the declaration: an object's
	// init list (P.pin=0, P.pin=1, ...) writes them per instance, so reading
	// the decl would print the template's pin for every object.
	value_t* v = csp_dio_slot(st, ix, DIN);
	int port, pin, dir;
	if (t == DECL_DIGITAL) { port = v->d.port; pin = v->d.pin; dir = v->d.dir; }
	else                   { port = v->a.port; pin = v->a.pin; dir = v->a.dir; }
	state_col(csp_fmt_pindir(dir), STATE_W_DIR);
	state_col((t == DECL_DIGITAL) ? "digital" : "analog", STATE_W_KIND);
	csp_print_uint(port); csp_print_char(':'); csp_print_uint(pin);
	state_pad(state_udigits(port) + 1 + state_udigits(pin), STATE_W_PIN);
    }
    csp_print_str("= ");
    if (!is_state || !state_print_state(st, csp_value(st, ix).i))
	csp_print_value(st, decl(st,di,vt), csp_value(st, ix));
    csp_println();
}

// Does declaration `di` pass the /state filters? Shared by the global and the
// per-object pass. A named filter matches on the DECLARATION, so `/state State`
// shows the global State and every object's State alike.
static int state_want(csp_rt_t* st, int di,
		      int f_var, int f_dig, int f_ana, int f_timer,
		      uint8_t dir_filter, const index_t* named, int nnamed)
{
    decl_t t = decl(st, di, type);
    const char* nm;

    if ((t != DECL_VARIABLE) && (t != DECL_DIGITAL) &&
	(t != DECL_ANALOG) && (t != DECL_TIMER))
	return 0;
    nm = decl_name(st, MAKE_INDEX(0, di));
    if (!nm || !*nm)
	return 0;
    if (nnamed) {
	int k;
	for (k = 0; k < nnamed; k++)
	    if (INDEX(named[k]) == (index_t)di)
		return 1;
	return 0;
    }
    if ((t == DECL_VARIABLE && !f_var) || (t == DECL_DIGITAL && !f_dig) ||
	(t == DECL_ANALOG && !f_ana)   || (t == DECL_TIMER && !f_timer))
	return 0;
    if (dir_filter && ((t == DECL_DIGITAL) || (t == DECL_ANALOG)) &&
	!(decl(st,di,dir) & dir_filter))
	return 0;
    return 1;
}

static int cmd_state(csp_rt_t* st, int argc, char* argv[])
{
    int i, a;
    int in_module = 0;
    int f_timer = 0, f_var = 0, f_dig = 0, f_ana = 0, any_cat = 0;
    uint8_t dir_filter = 0;    // 0 = any dir; else DIR_IN / DIR_OUT mask
    index_t named[MAX_ARGV];   // explicit "show just these" decl filters (OR)
    int nnamed = 0;

    if (!st->started) {   // -b before /resume: no leaves allocated yet
	csp_print_str("not started -- /resume to allocate and run\n");
	return CSP_CMD_OK;
    }

    // Tokens are OR'd: category words union, `in`/`out` narrow the direction,
    // bare names pick specific decls. `/state digital analog input` == input.
    for (a = 0; a < argc; a++) {
	const char* w = argv[a];
	if      (strcmp(w,"all")==0)     { f_timer=f_var=f_dig=f_ana=1; any_cat=1; }
	else if (strcmp(w,"timers")==0 || strcmp(w,"timer")==0)  { f_timer=1; any_cat=1; }
	else if (strcmp(w,"var")==0 || strcmp(w,"variables")==0 || strcmp(w,"variable")==0) { f_var=1; any_cat=1; }
	else if (strcmp(w,"digital")==0) { f_dig=1; any_cat=1; }
	else if (strcmp(w,"analog")==0)  { f_ana=1; any_cat=1; }
	else if (strcmp(w,"input")==0  || strcmp(w,"in")==0)  { f_dig=f_ana=1; dir_filter=DIR_IN;  any_cat=1; }
	else if (strcmp(w,"output")==0 || strcmp(w,"out")==0) { f_dig=f_ana=1; dir_filter=DIR_OUT; any_cat=1; }
	else {
	    const tstr_t sn = { (char*)w, strlen(w) };
	    index_t ix = csp_lookup_decl(st, &sn);
	    if ((ix != BAD_INDEX) && (nnamed < MAX_ARGV))
		named[nnamed++] = ix;
	    else {
		csp_print_str("unknown: "); csp_print_str(w); csp_print_char('\n');
	    }
	}
    }
    if (!any_cat && !nnamed)            // bare /state -> show everything
	f_timer = f_var = f_dig = f_ana = 1;

    // Status line -- the germ of a future terminal status bar. mode is one of
    // three mutually exclusive run states (paused and live can't both be on).
    csp_print_str("cycle ");
    csp_print_uint(st->cycle);
    csp_print_str("   latch ");
    csp_print_str(st->latch ? "on" : "off");
    csp_print_str("   ");
    csp_print_str(st->paused ? "paused" : st->live ? "live" : "running");
    csp_print_str("\n\n");

    // Two passes so object INSTANCES actually appear: the globals, then each
    // object with its own fields. The old walk indexed MAKE_INDEX(0,i) -- obj 0
    // only -- so it printed the module TEMPLATE once and never p0..p9, while the
    // timers came from st->timer[] (per-object, but named bare "T", so ten
    // indistinguishable rows).
    for (i = 0; i < st->ps.nd; i++) {
	decl_t t = decl(st,i,type);
	if (t == DECL_MODULE) { in_module = 1; continue; }
	if (t == DECL_END)    { in_module = 0; continue; }
	if (in_module)        continue;   // template fields belong to the objects
	if (!state_want(st, i, f_var, f_dig, f_ana, f_timer, dir_filter,
			named, nnamed))
	    continue;
	state_row(st, MAKE_INDEX(0, i), i);
    }

    for (i = 0; i < st->ps.nd; i++) {
	index_t mx;
	int dn, base, j, m, shown = 0;
	if (decl(st,i,type) != DECL_OBJECT)
	    continue;
	mx   = decl(st, i, mq.mx);
	m    = decl(st, i, mq.m);
	dn   = decl(st, INDEX(mx), md.n);
	base = INDEX(mx) + 1;
	for (j = 0; j < dn; j++) {
	    int dj = base + j;
	    decl_t t = decl(st, dj, type);
	    if (state_want(st, dj, f_var, f_dig, f_ana, f_timer, dir_filter,
			   named, nnamed)) {
		if (!shown) {           // header only once, and only if non-empty
		    csp_println();
		    csp_print_char('#');
		    csp_print_str(decl_name(st, MAKE_INDEX(0, i)));
		    csp_print_char(' ');
		    csp_print_str(decl_name(st, mx));
		    csp_println();
		    shown = 1;
		}
		state_row(st, MAKE_INDEX(m, dj), dj);
	    }
	    if (t == DECL_TIMER)   // a timer owns the next slot too (its t0)
		j++;
	}
    }
    return CSP_CMD_OK;
}

static int cmd_reset(csp_rt_t* st, int argc, char* argv[])
{
    (void)argv;
    csp_rebuild(st);
    csp_setup(st);
    csp_print_str("Reset\n");
    return CSP_CMD_OK;
}

// /pause -- freeze execution. The driver runs no cycle while paused, so you can
// inspect (/state, /list, /memory) and add rules/declarations. Edits are staged
// and integrated by /resume, keeping the running values until then.
static int cmd_pause(csp_rt_t* st, int argc, char* argv[])
{
    (void)argc; (void)argv;
    st->paused = 1;
    st->live = 0;
    csp_print_str("Paused (execution stopped; edit/inspect, then /resume)\n");
    return CSP_CMD_OK;
}

// /live -- freeze the rules but keep I/O running: inputs keep sampling (watch
// sensors with /state) and outputs keep being driven (> Led=1 lights the pin) so
// you can poke at the hardware while the program logic stands still. /resume goes
// back to running the rules.
static int cmd_live(csp_rt_t* st, int argc, char* argv[])
{
    (void)argc; (void)argv;
    if (!st->started) {
	csp_print_str("not started -- /resume first\n");
	return CSP_CMD_OK;
    }
    st->live = 1;
    st->paused = 0;
    csp_print_str("Live (rules frozen, I/O running -- poke away; /resume to run)\n");
    return CSP_CMD_OK;
}

// /resume -- continue execution. If the program was edited while paused, do one
// full rebuild first: the graph (reactive), leaf/device setup, and initial
// values -- so rules and declarations added while paused take effect.
static int cmd_resume(csp_rt_t* st, int argc, char* argv[])
{
    (void)argc; (void)argv;
    if (st->edited) {
	if (csp_rebuild(st) < 0) {   // say so: this used to fail silently
	    csp_print_str("Cannot resume: ");
	    csp_print_str(csp_format_error(st->ps.err));
	    csp_print_str("\n(the code pool must hold the program AND its data --"
			  " see /memory, raise -m)\n");
	    csp_clr_error(st);
	    return CSP_CMD_ERROR;
	}
	csp_setup(st);
	st->edited = 0;
	csp_print_str("Resumed (rebuilt)\n");
    }
    else {
	csp_print_str("Resumed\n");
    }
    st->paused = 0;
    st->live = 0;
    return CSP_CMD_OK;
}

// /clear -- drop all RAM patches so the firmware ROM baseline reappears.
// (RAM-added #states are not yet unwound -- see doc/ROM_RAM.md section 4.)
static int cmd_clear(csp_rt_t* st, int argc, char* argv[])
{
    (void)argc; (void)argv;
    st->ps.nd   = st->rom_nd;
    st->ps.nn   = st->rom_nn;
    st->ps.strp = st->rom_strp;
    st->ps.nq   = 0;
    csp_rebuild(st);
    csp_setup(st);
    csp_print_str("Cleared RAM patches -- ROM restored\n");
    return CSP_CMD_OK;
}

// print v right-aligned in a field of width w (v assumed >= 0)
static void mem_int_r(int v, int w)
{
    int n = 1, t = v;
    while (t >= 10) { n++; t /= 10; }
    while (n++ < w) csp_print_char(' ');
    csp_print_int(v);
}

static void mem_name(const char* name)
{
    int len = 0;
    csp_print_str("  ");
    csp_print_str(name);
    while (name[len]) len++;
    while (len++ < 9) csp_print_char(' ');
}

// One /memory row: NAME  used  limit [ pct ]. `limit` < 0 means there is no
// ceiling -- the row is sized to whatever the program needs -- and prints "-".
// pct is only meaningful against a real limit.
static void mem_row(const char* name, uint32_t used, int32_t limit, int show_pct)
{
    mem_name(name);
    mem_int_r((int)used, 8);
    if (limit < 0) {
	int k;
	for (k = 1; k < 9; k++) csp_print_char(' ');
	csp_print_char('-');
    }
    else
	mem_int_r(limit, 9);
    if (show_pct && (limit > 0)) {
	mem_int_r((int)((used * 100) / (uint32_t)limit), 5);
	csp_print_char('%');
    }
}

// Bytes handed out of the middle region. The bump cursor already knows exactly,
// alignment padding included -- no need to re-add up the individual tables and
// hope the sum tracks what was actually allocated.
// A flat "name  value" row for the RAM breakdown (no ceiling, no percent).
static void mem_val(const char* name, uint32_t v)
{
    mem_name(name);
    mem_int_r((int)v, 8);
    csp_println();
}

NOINLINE static uint32_t csp_derived_bytes(csp_rt_t* st)
{
    return (uint32_t)(st->mid - st->mid_base);
}

// What CandySpeak itself costs the board's RAM, computed from our OWN structures
// so it means the same thing on host and target. The backend's ram_used() cannot
// answer this on the host: it measures the whole PROCESS (libc, stdio, the host's
// own oversized arena), so comparing that against a simulated -M is meaningless
// -- it read 338% of a mega for a program that fits. This is arena + state +
// derived. The stack is deliberately NOT in here: it is measured, not computed.
// /memory: what CandySpeak costs and what binds it. Three ceilings matter and
// they are different: the board's RAM, the code pool's byte budget (mem_fits),
// and the EEPROM -- code that will not fit the last cannot be saved, only tested.
// The category table below is counts, not bytes: decl/instr/string live in the
// RAM arrays (ROM stays in flash) so those show the RAM-patch usage; the rest are
// rebuilt whole into RAM arrays, so their counts include any ROM content.
static int cmd_memory(csp_rt_t* st, int argc, char* argv[])
{
    size_t ib = (size_t)(st->ps.nn - st->rom_nn) * sizeof(csp_instr_t);
    size_t db = (size_t)(st->ps.nd - st->rom_nd) * sizeof(csp_decl_t);
    size_t used = ib + db;
    (void)argc; (void)argv;

    // Where the board's RAM went. Since CandySpeak claims all free RAM, a
    // "used / total" line would always read 100% -- useless. What is worth seeing
    // is the OVERHEAD: what everything except your program's code costs, because
    // that is what decides how much is left to grow into, and what a linked
    // library actually costs you. These sum to capacity.
    //   system   Arduino core + libraries + startup + C++ globals (moves when you
    //            link CircuitPlayground, a CAN driver, ...)
    //   struct   the csp_rt_t runtime state
    //   buffers  CandySpeak's derived tables (view/heap/buf/graph/inq/pending/...)
    //   stack    reserved
    //   code     your program: instr + decl
    //   free     spare -- room for a bigger program or more data
    {
	uint32_t cap     = csp_system_ram_capacity();
	uint32_t sys     = csp_system_ram_used();
	uint32_t buffers = csp_derived_bytes(st);
	uint32_t acc     = sys + model_state() + buffers
			 + CSP_STACK_RESERVE + (uint32_t)used;
	uint32_t freeram = (cap > acc) ? (cap - acc) : 0;
	csp_print_str("RAM ");
	csp_print_uint((uvalue_t)cap);
	csp_print_str(" total:\n");
	mem_val("system",  sys);
	mem_val("struct",  model_state());
	mem_name("buffers"); mem_int_r((int)buffers, 8);
	if (!st->started) csp_print_str("   (allocated on /resume)");
	csp_println();
	mem_val("stack",   CSP_STACK_RESERVE);
	mem_val("code",    (uint32_t)used);
	mem_val("free",    freeram);
    }

    {
	uint32_t cap  = csp_eeprom_capacity();
	uint32_t need = (uint32_t)csp_eeprom_size(st);
	if (cap == CSP_EEPROM_NONE) {
	    mem_row("EEPROM", need, 0, 0);
	    csp_print_str("   (NONE)");
	}
	else if (cap == CSP_EEPROM_UNBOUNDED) {
	    mem_row("EEPROM", need, -1, 0);
	    csp_print_str("   (OK)");
	}
	else {
	    mem_row("EEPROM", need, (int32_t)cap, 1);
	    csp_print_str((need > cap) ? "  (FULL)" : "  (OK)");
	}
	csp_println();
    }

    if (st->rom_nd || st->rom_nn || st->rom_strp) {
	csp_println();
	csp_print_str("  ROM base   ");
	csp_print_int(st->rom_nd);   csp_print_str(" decl, ");
	csp_print_int(st->rom_nn);   csp_print_str(" instr, ");
	csp_print_int(st->rom_strp); csp_print_str(" str\n");
    }

    csp_println();
    mem_row("instr",   st->ps.nn   - st->rom_nn,   MAX_INSTRS, 0);   csp_println();
    mem_row("decl",    st->ps.nd   - st->rom_nd,   MAX_DECLS,  0);   csp_println();
    mem_row("string",  st->ps.strp - st->rom_strp, MAX_STR_BUF, 0);  csp_println();
    mem_row("objects", st->ps.nq,  MAX_OBJECTS, 0);                  csp_println();
    mem_row("modules", st->nm,     MAX_MODULES, 0);                  csp_println();
    mem_row("states",  st->ps.ns,  MAX_STATES,  0);                  csp_println();
    mem_row("in",      st->ni,   -1, 0);                             csp_println();
    mem_row("out",     st->no,   -1, 0);                             csp_println();
    mem_row("timers",  st->nt,   -1, 0);                             csp_println();
    mem_row("buffers", st->nbuf, -1, 0);                             csp_println();
    return CSP_CMD_OK;
}

static int cmd_commit(csp_rt_t* st, int argc, char* argv[])
{
    (void)argv;
    csp_commit(st);
    csp_print_str("Committed\n");
    return CSP_CMD_OK;
}

static int cmd_quit(csp_rt_t* st, int argc, char* argv[])
{
    (void)st; (void)argv;
    return CSP_CMD_QUIT;
}

void csp_cmd_help(void)
{
    cmd_help(NULL, 0, NULL);
}

static int cmd_latch(csp_rt_t* st, int argc, char* argv[])
{
    int latch = 0;
    if ((argc == 1) && (strcmp(argv[0], "on") == 0))
	latch = 1;
    else if ((argc == 1) && (strcmp(argv[0], "off") == 0))
	latch = 0;
    else
	return CSP_CMD_ERROR;
    csp_set_latch(st, latch);
    return CSP_CMD_OK;
}

// dispatch cmd, note that cmd is written to!
int csp_cmd_dispatch(csp_rt_t* st, char* cmd)
{
    char* ptr = cmd;
    const csp_cmd_t* c;
    int namelen;
    char* argv[MAX_ARGV];
    int argc = 0;
    
    // Skip command name to find args
    while (*ptr && !ISBLANK(*ptr)) ptr++;
    namelen = ptr - cmd;
    while (ISBLANK(*ptr)) ptr++;
    
    while((*ptr) && (argc < MAX_ARGV)) {
	// advance ptr to next arg
	char *arg = ptr;
	while (*ptr && !ISBLANK(*ptr)) ptr++;
	if (*ptr) *ptr++ = '\0';
	while (ISBLANK(*ptr)) ptr++;
	argv[argc++] = arg;
    }
    argv[argc] = NULL;
    for (c = builtin_cmds; c->name; c++) {
	if ((strncmp(cmd, c->name, namelen) == 0) &&
	    (c->name[namelen] == '\0')) {
	    return c->fn(st, argc, argv);
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

    if (!st->started) {   // -b before /resume: value slots not allocated yet
	csp_print_str("not started -- /resume to allocate and run\n");
	return -1;
    }

    if (csp_scan_line(st, line, tv, &num) < 0) {
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
    csp_pmark_t pm;
    index_t nd0 = st->ps.nd;

    csp_pstate_save(st, &pm);
    if (csp_parse(st, line) < 0) {
	csp_print_str("Error: ");
	csp_print_str(csp_format_error(st->ps.err));
	csp_print_char('\n');
	csp_clr_error(st);
	// Rewind. Inside an unclosed module the whole module goes: otherwise
	// mdef stays set with no #end in sight and every following line is
	// swallowed by a module that can never be closed.
	if (st->mdef != BAD_INDEX) {
	    csp_pstate_restore(st, &st->mod_mark);
	    csp_print_str("Module aborted\n");
	}
	else
	    csp_pstate_restore(st, &pm);
	return -1;
    }
    // A new declaration grows the leaf space, so once started we must re-run
    // rt_start now to keep view/dset/buf/heap sized to it (a stale, too-small
    // view would be read out of bounds). Values re-init as they always do on a
    // declaration.
    if ((st->ps.nd != nd0) && st->started && !st->paused) {
	csp_rebuild(st);            // clears edited
	csp_setup(st);
    }
    else {
	// Any other add -- rule-only, paused, live, or before /resume. Mark it
	// and let the rebuild happen where it is safe: /resume, or the top of
	// the next cycle. A rule-only add used to skip this entirely, which left
	// the new rule without graph edges: reactively it never fired.
	st->edited = 1;
    }
    csp_print_str("OK\n");
    return 0;
}

int csp_process_line(csp_rt_t* st, char* line)
{
    int len;
    
    // Skip leading whitespace
    while (*line && (*line == ' ' || *line == '\t')) line++;
    if (*line == '\0' || *line == '\n')
	return CSP_CMD_OK;
    // Remove trailing newline
    len = strlen(line);
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
	// Bare line: a persistent rule (stored, captured by /save) if it either
	// assigns (top-level '=') or carries a '?' guard -- e.g. a bare action
	// like `println("hi") ? Idx==1`, which has no '=' at all. A plain
	// expression (a query) has neither and is evaluated once.
	token_t tv[MAX_LINE_TOKENS];
	size_t num = MAX_LINE_TOKENS;
	int is_rule = 0;
	if (csp_scan_line(st, line, tv, &num) > 0) {
	    size_t k;
	    for (k = 0; k < num; k++) {
		if (tv[k].t == EQ || tv[k].t == QUEST) { is_rule = 1; break; }
	    }
	}
	if (is_rule)
	    csp_process_persistent(st, line);
	else
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
