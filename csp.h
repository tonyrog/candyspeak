#ifndef __CSP_H__
#define __CSP_H__

#include <stdint.h>
#include <stdlib.h>

#include "csp_config.h"

#ifndef EXTERN_C_BEGIN
#define EXTERN_C_BEGIN  extern "C" {
#define EXTERN_C_END    }
#endif

#ifdef __cplusplus
EXTERN_C_BEGIN
#endif

// an index has the following structure
// obj:4, index:12   // declaration object
//
// obj is current object 0 = global, cur = 2^OBJ_BITS-1 or the actual obj index
// tag is TAG_DECL (offs[m]+st->decl[index])
// 

typedef uint16_t index_t;  // sizeof type >= INDEX_BITS
typedef uint8_t  reg_t;    // at most 256 registers

#define OBJ_BITS     4    // (2^OBJ_BITS-2) (14)
#define DECL_BITS    10
#if defined(__AVR__)
#define INSTR_BITS   5
#define MAX_DECLS    32
#else
#define INSTR_BITS   9
#define MAX_DECLS    128 // (less than MAX_INDICES keep power of 2!!
#endif
#define INDEX_BITS   (OBJ_BITS+DECL_BITS)
#define REG_BITS     4
#define GLOBAL       0                       // global level
#define CURRENT      ((1 << OBJ_BITS)-1)     // current obj
#define MAX_INDICES  (1 << INDEX_BITS)
#define MAX_REGS     (1 << REG_BITS)
#define MAX_INSTRS   (1 << INSTR_BITS)
#define MAX_INPUTS   32  // <= then MAX_DECLS
#define MAX_OUTPUTS  32  // <= then MAX_DECLS
#define MAX_TIMERS   16  // <= then MAX_DECLS
#define MAX_MODULES  (1 << OBJ_BITS)
#define MAX_OBJECTS  (1 << OBJ_BITS)
#define MAX_QUEUE    (MAX_INSTRS)
#define MAX_INDEX    (MAX_INSTRS+1)
#define MAX_STACK_DEPTH 4
#define STRING_BITS  9
#define NAME_BITS    5
#define MAX_STR_BUF  (1 << STRING_BITS) // total number of char in var names
#define MAX_NAME_LEN 8     // max var name len
#define MAX_ARGS     4     // max number of arguments to function
#define MAX_USER_FUNCS 16  // max user-defined functions

#define BAD_INDEX   (MAX_INDICES-1)
#define PARSE_ERROR -1

#define INDEX(n)  ((n) & ((1 << DECL_BITS)-1))
#define OBJ(n)    ((n) >> DECL_BITS)
#define MAKE_INDEX(obj,x) (((obj)<<DECL_BITS) | (x))

#define MAX_PARSE_STACK_DEPTH 10
#ifdef CSP_EMBEDDED
#define MAX_LINE_TOKENS 24
#else
#define MAX_LINE_TOKENS 64
#endif

#define CSP_TRUE  -1  // all bits set, like openCL/Forth
#define CSP_FALSE 0

#define TYPE_BITS 3  // supports up to 8 types

typedef enum {
    V_VOID     = 0,  // value / don't care
    V_INTEGER  = 1,  // signed integer
    V_UNSIGNED = 2,  // unsigned integer
    V_FLOAT    = 3,  // floating point
    V_STRING   = 4,  // string index
    V_INDEX    = 5,  // declaration index (pass index, not value)
    // match types (not passed in type code)
    V_NUMBER   = 6,  // V_INTEGER | V_FLOAT
    V_ANY      = 7,  // 7 - V_INTEGER | V_FLOAT | V_STRING | V_INDEX    
} vtype_t;

typedef enum {
    E_UNDEFINED,
    E_LITTLE,
    E_BIG,
} vendian_t;

typedef int32_t  ivalue_t;
typedef uint32_t uvalue_t;
typedef int32_t  sindex_t;

#if defined(USE_FIXPOINT) && (USE_FIXPOINT == 1)
#include "csp_fixpoint.h"
typedef fixpoint_t fvalue_t;
#define FVALUE_IS_FIXPOINT 1
#else
typedef float fvalue_t;
#define FVALUE_IS_FIXPOINT 0
#endif

typedef union {
    ivalue_t i;  // V_INTEGER
    uvalue_t u;  // V_UNSIGNED
    fvalue_t f;  // V_FLOAT
    sindex_t s;  // V_STRING (index into string buf)
} value_t;

// require csp_rt_init!
//#define ZERO MAKE_INDEX(0,0)
//#define ONE  MAKE_INDEX(0,1)

typedef uint32_t set_group_t;  // bit set element
#define BITSET_GROUP_BITS (8*sizeof(set_group_t))
#define BITSET_GROUPS(size) (((size)+BITSET_GROUP_BITS-1)/BITSET_GROUP_BITS)
#define BITSET_GROUP(i) ((i)/BITSET_GROUP_BITS)
#define BITSET_BIT(i)   (1 << ((i)%BITSET_GROUP_BITS))

#define bitset_decl(name,size) set_group_t (name)[BITSET_GROUPS(size)]
#define bitset_zero(name) memset(&(name), 0x00, sizeof(name))
#define bitset_set(name,i) (name)[BITSET_GROUP((i))] |= BITSET_BIT((i))
#define bitset_clr(name,i) (name)[BITSET_GROUP((i))] &= ~BITSET_BIT((i))
#define bitset_tst(name,i) (((name)[BITSET_GROUP((i))] & BITSET_BIT((i)))!=0)

typedef enum {
    NONE = 0,  // empty
    // leafs
    MODULE,   // 'module'
    END,      // 'end'
    CONSTANT, // 'constant'
    VARIABLE, // 'variable'
    DIGITAL,  // 'digital'
    ANALOG,   // 'analog'
    TIMER,    // 'timer'
    CAN,      // 'can'
    UART,     // 'uart'
    SOCKET,   // 'socket'
    MOD,      // module instance
    //
    FIRST_NODE, // built-in + operators start
    // node - unary
    EXCLAMATION, // "!"  x=-y == x=0-y
    TILDE,       // "~"  x=~y =  x=1^y        
    MINUS1,      // "-"  x=-y == x=0-y
    PLUS1,       // "+"  x=+y == x=0+y
    // node - binary operator
    PLUS,      // "+"
    MINUS,     // "-"
    ASTERISK,  // "*"
    SLASH,     // "/"
    PERCENT,   // "%"
    LTLT,    // "<<"
    GTGT,    // ">>"    
    LT,      // "<"
    LTEQ,    // "<="
    GT,      // ">"
    GTEQ,    // ">="
    EQEQ,    // "=="
    NEQ,     // "!="    
    AMP,     // "&"
    BAR,     // "|"
    CIRC,    // "^"
    AMPAMP,  // "&&"
    BARBAR,  // "||"
    EQ,      // "="
    COMMA,   // ","
    // query rule/operator
    QUEST,   // "?"
    // other
    NEXT,
    ENTER,
    LEAVE,
    NEW,
    CALL,
    LD,
    ST,
    LDI,
    ARG,
    CVTIF,
    CVTFI,    
    // functions are now handled via OP_CALL + function table
    LAST_NODE, // built-in + operators stop
    // keywords
    PULLUP,   // 'pullup'
    PULLDOWN, // 'pulldown'
    RESOLUTION, // 'resolution'
    IN,         // 'in'
    OUT,        // 'out'
    INOUT,      // 'inout'
    PWM,        // 'pwm'
    FLOAT,      // 'float'
    INTEGER,    // 'integer'
    UNSIGNED,   // 'unsigned'
    STRING,     // 'string'
    LITTLE,     // 'little'
    BIG,        // 'big'

    // tokens
    LP,      // "("
    RP,      // ")"
    COLON,   // ":"
    HASH,    // "#"
    DOT,     // "."
    LB,      // "["
    RB,      // "]"
    INT,     // 123 | 0x9ab
    FLT,     // 0.123
    STR,     // "abc"
    WORD,    // abc
    NEWLINE, // \n \r \r\n
    LAST,
} tok_t;

typedef enum {
    TOKT_DECL,
    TOKT_INSTR,
    TOKT_TOKEN
} tok_type_t;

typedef union
{
    struct {
	char* ptr;
	int len;
    } str;
    value_t val;
} tokval_t;

// combined token and value
typedef struct
{
    tok_t    t;
    tokval_t v;
} token_t;

typedef enum {
    OP_NOP = 0,  // nothing
    OP_NOT,     // "!"  x=-y == x=0-y
    OP_BNOT,    // "~"  x=~y =  x=1^y        
    OP_NEG,     // "-"  x=-y == x=0-y
    OP_POS,     // "+"  x=+y == x=0+y
    OP_CVTIF,   // trunc float => integer
    OP_CVTFI,   // cast int to float
    // node - binary operator
    OP_ADD,     // "+"
    OP_SUB,     // "-"
    OP_MUL,     // "*"
    OP_DIV,     // "/"
    OP_REM,     // "%"
    OP_SLA,     // "<<"
    OP_SRA,     // ">>"    
    OP_LT,      // "<"
    OP_LTE,     // "<="
    OP_GT,      // ">"
    OP_GTE,     // ">="
    OP_EQEQ,    // "=="
    OP_NEQ,     // "!="
    OP_BAND,    // "&"
    OP_BOR,     // "|"
    OP_BXOR,    // "^"
    OP_AND,     // "&&"
    OP_OR,      // "||"

    OP_FNEG,     // "-"  x=-y == x=0-y    
    OP_FADD,     // "+"
    OP_FSUB,     // "-"
    OP_FMUL,     // "*"
    OP_FDIV,     // "/"

    OP_FLT,      // "<"
    OP_FLTE,     // "<="
    OP_FGT,      // ">"
    OP_FGTE,     // ">="
    OP_FEQEQ,    // "=="
    OP_FNEQ,     // "!="    
    
    OP_EQ,      // "="
    OP_COMMA,   // ","
    // rule
    OP_RULE,    // "?"
    OP_NEXT,    // "next"
    // generate ops from MODULE/END
    OP_ENTER,   //
    OP_LEAVE,   //
    OP_NEW,     // #<module> <instance-name>
    OP_LD,      // load register from memory
    OP_ST,      // store register to memory
    OP_LI,      // load signed 16-bit constant
    OP_LIU,     // load unsigned 16-bit constant (zero extend)
    OP_LIH,     // load high 16-bit (OR into high bits)
    OP_ARG,     // load argument from register
    OP_CALL,    // function call:
    OP_LAST,
} opcode_t;

typedef struct
{
    tok_t  tok;
    tok_type_t ttype;
    int8_t code;
    const char* name;
    uint8_t name_len;
    int8_t arity;
    int8_t prec;
    int8_t assoc;
} op_entry_t;


// Forward declarations
struct _csp_rt_t;
struct csp_instr;

typedef enum {
    DECL_NOP=0,    // emtpy declaration
    DECL_MODULE,   // 'module'
    DECL_END,      // 'end'
    DECL_OBJECT,   // module instance
    DECL_CONSTANT, // 'constant'
    DECL_VARIABLE, // 'variable'
    DECL_DIGITAL,  // 'digital'
    DECL_ANALOG,   // 'analog'
    DECL_TIMER,    // 'timer'
    DECL_CAN,      // 'can'
    DECL_UART,     // 'uart'
    DECL_SOCKET,   // 'socket'
} decl_t;

// #define IS_DECL(i)  (TAG((i)) == TAG_DECL)
// #define IS_REG(i)   (TAG((i)) == TAG_REG)

#define DECL_TYPE(s,i) ((s)->decl[(i)].type)
//#define IS_QVAR(s,i)   (DECL_TYPE((s),(i))==DECL_VARIABLE)
#define IS_CONST(s,i)  (DECL_TYPE((s),(i))==DECL_CONSTANT)
#define IS_MODULE(s,i) (DECL_TYPE((s),(i))==DECL_MODULE)
#define IS_OBJECT(s,i) (DECL_TYPE((s),(i))==DECL_OBJECT)
#define IS_END(s,i)    (DECL_TYPE((s),(i))==DECL_END)
#define IS_CAN(s,i)    (DECL_TYPE((s),(i))==DECL_CAN)

#define MAKE_RES(r) ((r)-1)
#define GET_RES(rr) ((rr)+1)

#define MAKE_CAN_LEN(len) ((len)-1)
#define GET_CAN_LEN(len) ((len)+1)

#define NOTIMEOUT 0xffffffff

#define PACKED __attribute__((packed))

typedef struct PACKED {
    index_t n;          // number of nodes in module definition
    index_t ent;        // entry point in instr
} csp_module_t;

typedef struct PACKED {
    index_t  mx;           // module declaration index
    unsigned m:OBJ_BITS;   // index in object table
} csp_object_t;

typedef struct PACKED  {
    value_t init;    // init value
} csp_variable_t;

typedef struct PACKED  {
    value_t init;   // constant value
} csp_constant_t;

typedef struct PACKED  {
    unsigned pin:8;
    unsigned port:8;
    unsigned pullup:1;
    unsigned pulldown:1;
    // init?
} csp_digital_t;

typedef struct PACKED {
    unsigned pin:8;
    unsigned port:8;
    unsigned pwm:1;    // pwmoutput
    // init?    
} csp_analog_t;

typedef struct PACKED {
    unsigned id:INDEX_BITS; // variable | constant (unsigned) 11/29 bit
    unsigned endian:2; // |little|big
    unsigned bit:9;   // 0-511   // bit start pos
    unsigned len:5;   // (1-32)  // data length -1
} csp_can_t;

typedef struct PACKED {
    unsigned running:1;     // != 0 if timer is running
    unsigned init:1;        // initial value if given
    unsigned px:INDEX_BITS; // variable | constant (unsigned)
    unsigned tx:INDEX_BITS; // start time tick (intern variable)
} csp_timer_t;

// new instruction format
// general operations OP_ADD ...
typedef struct PACKED {
    opcode_t op:6;    
    unsigned x:REG_BITS;
    unsigned y:REG_BITS;
    unsigned z:REG_BITS;
} csp_instr_alu_t;

// op = ST | LD
// load or store register from memory
typedef struct PACKED {
    opcode_t op:6;    
    unsigned x:REG_BITS;
    unsigned mem:INDEX_BITS;  // declaration: variable/constant
} csp_instr_mem_t;

// op LDI / ARG
// load immediate LDI load small 16 bit signed constant
typedef struct PACKED {
    opcode_t op:6;    
    unsigned x:REG_BITS;
    signed imm:16;
} csp_instr_imm_t;

typedef struct PACKED {
    opcode_t op:6;    
    unsigned cnd:REG_BITS; // condition register
    int16_t nxt;           // jump if !cnd
} csp_instr_rule_t;

typedef struct PACKED {
    opcode_t op:6;
    unsigned num:INSTR_BITS;  // number of instructions
    index_t  mx;     // module index
} csp_instr_enter_t;

typedef struct PACKED {
    opcode_t op:6;    
    unsigned num:INSTR_BITS;  // number of instructions
    index_t  mx;     // module index
} csp_instr_leave_t;

typedef struct PACKED {
    opcode_t op:6;    
    unsigned ent:INSTR_BITS; // entry point index in instr[]
    index_t  obj;            // object declaration index 
} csp_instr_new_t;

typedef struct PACKED {     // 4+4+1+16 = 25
    opcode_t op:6;    
    unsigned x:REG_BITS;    // result register    
    unsigned idx:REG_BITS;  // function index
    unsigned usr:1;         // user function
    unsigned avt:16;        // argument value types 4 bit per argument
} csp_instr_call_t;

typedef union {
    // uint32 need on arduino uno (unsigned is 16 bit?)
    struct PACKED { opcode_t op:6; uint32_t rest:26; };
    csp_instr_enter_t e;
    csp_instr_leave_t v;
    csp_instr_new_t n;
    csp_instr_imm_t i;
    csp_instr_mem_t m;
    csp_instr_call_t f;
    csp_instr_rule_t r;
    csp_instr_alu_t a;
} csp_instr_t;

typedef enum {
    DIR_NONE = 0x00,
    DIR_IN  = 0x01,
    DIR_OUT = 0x02,
    DIR_INOUT = 0x03
} pindir_t;
    

typedef struct PACKED {
    decl_t type:6;                 // DECL_xxx
    pindir_t dir:2;                // IN/OUT    
    unsigned name:STRING_BITS;     // string index
    unsigned vt:TYPE_BITS;         // value type (vtype_t)
    unsigned res:5;                // 1-32  (use MAKE_RES)
    unsigned is_mapped:1;          // compiletime: 1 iff reg is valid value
    unsigned reg:REG_BITS;         // var/constant loaded in register
    union PACKED {
	csp_module_t   md;
	csp_object_t   mq;
	csp_variable_t va;
	csp_constant_t cn;
	csp_digital_t  di;
	csp_analog_t   an;
	csp_can_t      ca;
	csp_timer_t    tm;
    };
} csp_decl_t;

typedef enum {
    ERR_OK = 0,
    ERR_SYNTAX,
    ERR_STRING_SPACE_EXHUSTED,    
    ERR_TOO_MANY_DECLARATIONS,
    ERR_TOO_MANY_INSTRUCTIONS,
    ERR_TOO_MANY_OBJECTS,
    ERR_MODULE_NOT_DECLARED,
    ERR_NOT_A_MODULE,
    ERR_OBJECT_ALREADY_DEFINED,
    ERR_OBJECT_NOT_DEFINED,
    ERR_VARIABLE_NOT_DECLARED,    
} csp_err_t;

// parser state, save state before parse
// so that restore may be possible when error
typedef struct PACKED {
    index_t nn;                  // number of instructions
    index_t nd;                  // number of decls
    index_t nq;                  // number of objects
    uint32_t strp;               // string table position
    csp_err_t err;               // error code
    uint32_t line;               // line number when parsing
} csp_pstate_t;

// Function pointer types
typedef value_t (*csp_func_fn)(struct _csp_rt_t* st, uint16_t type,
			       value_t* args, uint8_t nargs);

typedef int (*csp_const_fn)(struct _csp_rt_t* st, const char* name, int len,
			    value_t*, vtype_t*);

// Function table entry
typedef struct {
    const char* name;
    uint8_t namelen;
    uint8_t nargs;              // number of arguments (0-4)
    vtype_t rtype;              // return type
    vtype_t argtypes[MAX_ARGS]; // argument types (V_INTEGER, V_INDEX, etc)
    csp_func_fn fn;             // function to call
} csp_func_t;


typedef struct
{
    reg_t   free_regs[MAX_REGS];
    index_t rmap[MAX_REGS];
    int top;
    int temp_top;
    int pin_top;
} reg_allocator_t;


typedef struct _csp_rt_t
{
    csp_instr_t instr[MAX_INSTRS]; // instructions used
    csp_decl_t decl[MAX_DECLS];    // declarations used

    value_t* din;                  // din point to dv0 or dv1
    value_t* dout;                 // dout point to dv0 or dv1
    value_t reg[MAX_REGS];         // register area
    value_t arg[MAX_ARGS];         // loaded before call
    
    value_t dv0[MAX_INDEX];       // declaration (leaf) value (y,z)    
#if defined(SUPPORT_TRANSACTION) && (SUPPORT_TRANSACTION==1)
    value_t dv1[MAX_INDEX];       // declaration (leaf) value (y,z)    
#endif
    // check if any node has been set: anyx|anyd == CSP_TRUE
    int8_t  anyd;  // CSP_TRUE|CSP_FALSE
    bitset_decl(dset, MAX_INDEX); // mark decl updated during cycle
    
    char    str[MAX_STR_BUF];      // store variable names    
    index_t offs[MAX_OBJECTS];     // offset to object locals
    // stack used during eval
    int esp;                       // eval stack pointer
    struct PACKED { index_t ix; unsigned cur:OBJ_BITS; }
	stack[MAX_STACK_DEPTH];
    unsigned transaction:1;      // 1 if keeping a log
    unsigned reactive:1;         // 1 if push backedges to queue

    csp_pstate_t ps;             // parse state
    reg_allocator_t* ap;
    
    index_t mdef;                // module being defined
    int     ent;                 // entry op of module in st->instr
    unsigned cur:OBJ_BITS;       // current module index

    // calculated by csp_rt_start
    index_t nt;                  // number of timers
    index_t ni;                  // number of input
    index_t no;                  // number of output
    index_t nm;                  // number of modules
    index_t input[MAX_INPUTS];     // list of inputs (digital/analog ...)
    index_t output[MAX_OUTPUTS];   // list of outputs (digital/analog ...)
    index_t module[MAX_MODULES];   // list of modules
    index_t object[MAX_OBJECTS];   // list of objects
    index_t timer[MAX_TIMERS];     // list of timers
    // during eval
    uint32_t update;             // update counter
    uint32_t wait_ms;            // sleep time or NOTIMEOUT
#if defined(SUPPORT_REACTIVE) && (SUPPORT_REACTIVE==1)        
    bitset_decl(inq, MAX_INDEX); // mark nodes in queue during eval
    index_t queue[MAX_QUEUE];    // nodes in queue
    int hd,tl;  // queue head and tail
    // back references
    index_t idg[MAX_INDEX];    // in degree per instr
    index_t ofs [MAX_INDEX+1]; // output offset from each instr
    index_t edg [MAX_INDEX+1]; // edg[ofs[n]+0...ideg[n]-1] back pointer
#endif
    uint32_t cycle;
#if defined(USE_STATISTICS) && (USE_STATISTICS==1)
    uint32_t num_eval_rule;    
    uint32_t num_eval0;
#endif
    // user-defined functions (checked before builtin)
    const csp_func_t* ufuncs;
    uint8_t num_ufuncs;
    // user hook to lookup platform constants
    csp_const_fn uconst;
} csp_rt_t;

// Parser stack entry - tracks both register and declaration index
typedef struct PACKED {
    value_t val;     // if constant then the actual value is loaded here
    index_t ix;      // declaration index (valid for variables)    
    reg_t reg;       // register number (valid if loaded)
    union {
	// uint8_t vtf;     // vt + flags(soon)
	struct {
	    unsigned vt:TYPE_BITS;
	    unsigned L:1;    // == 1 when reg is valid (loaded)
	    unsigned I:1;    // == 1 when val is immediate value
	    unsigned X:1;    // == 1 when ix is decl index
	};
    };
} rentry_t;

// Built-in function table (defined in csp_rt.c)
extern const csp_func_t csp_builtin_funcs[];
extern const uint8_t csp_num_builtin_funcs;

static inline int st_index(csp_rt_t* st, index_t n)
{
    return st->offs[OBJ(n)] + INDEX(n);
}

#if defined(SUPPORT_REACTIVE) && (SUPPORT_REACTIVE==1)    
// enq an node for recalculation
static inline void csp_enq(csp_rt_t* st, uint16_t ip)
{
    if (bitset_tst(st->inq,ip))
	return;
    if ((st->tl - st->hd) != MAX_QUEUE) {
	st->queue[st->tl % MAX_QUEUE] = ip;
	st->tl++;
	bitset_set(st->inq, ip);
    }
}

static inline index_t csp_deq(csp_rt_t* st)
{
    index_t x;
    if (st->tl == st->hd)
	return BAD_INDEX;
    x = st->queue[st->hd % MAX_QUEUE];
    st->hd++;
    // don't clear inq bit here - cleared at cycle start to prevent
    // same rule being queued multiple times within a cycle
    return x;
}
#endif

static inline value_t csp_value(csp_rt_t* st, index_t x)
{
    return st->din[st_index(st, x)];
}

static inline ivalue_t csp_ivalue(csp_rt_t* st, index_t ix)
{
    value_t v = csp_value(st, ix);
    return v.i;
}

static inline uvalue_t csp_uvalue(csp_rt_t* st, index_t ix)
{
    value_t v = csp_value(st, ix);
    return v.u;    
}

static inline fvalue_t csp_fvalue(csp_rt_t* st, index_t ix)
{
    value_t v = csp_value(st, ix);
    return v.f;        
}

static inline char* decl_name(csp_rt_t* st, index_t ix)
{
    return &st->str[st->decl[INDEX(ix)].name];
}

extern int     csp_rt_init(csp_rt_t*,  int transaction, int reactive);
extern int     csp_rt_start(csp_rt_t*);
extern void    csp_set_ufuncs(csp_rt_t*, const csp_func_t*, uint8_t);
extern void    csp_set_uconst(csp_rt_t*, csp_const_fn uconst);
extern int     csp_lookup_func(csp_rt_t*, const char*, uint8_t);
extern int     csp_set_transaction(csp_rt_t*, int onoff);
extern int     csp_set_reactive(csp_rt_t*, int onoff);
extern int     csp_scan_line(char* str, token_t* tv, size_t* num_toks);
extern int     csp_parse(csp_rt_t*, char* str);
extern void    csp_csr(csp_rt_t* st);
extern index_t csp_eval(csp_rt_t* st);
extern int     csp_eval_rule(csp_rt_t* st, int);
extern index_t csp_react(csp_rt_t* st);
extern void    csp_undo(csp_rt_t* st);
extern void    csp_commit(csp_rt_t* st);

extern void csp_set_value(csp_rt_t* st, index_t n, value_t v);
extern void csp_set_ivalue(csp_rt_t* st, index_t n, ivalue_t v);
extern void csp_set_fvalue(csp_rt_t* st, index_t n, fvalue_t v);

extern int csp_parse_expr(csp_rt_t* st, token_t* tv, size_t* num_toks,
			  rentry_t* result);
//
extern index_t csp_new_decl(csp_rt_t* st, char* name, int name_len, decl_t op);
extern index_t csp_lookup_decl(csp_rt_t* st, char* module, char* name);

// backend port (linux/arduino/LPCopen/FreeRTOS)
extern uint32_t csp_time_ms(void);
extern unsigned long csp_time_us(void);
extern void csp_setup(csp_rt_t* st);
extern void csp_input(csp_rt_t* st);
extern void csp_output(csp_rt_t* st);

// eeprom save/load (csp_eeprom.c)
extern int csp_eeprom_save(csp_rt_t* st);
extern int csp_eeprom_load(csp_rt_t* st);
extern int csp_eeprom_size(csp_rt_t* st);

// stack check/debug
extern int stack_used();

// platform print functions
extern int csp_print_char(char c);
extern int csp_print_str(const char* s);
extern int csp_print_int(ivalue_t v);
extern int csp_print_uint(uvalue_t v);
extern int csp_print_float(fvalue_t v);
extern int csp_print_hex(uvalue_t v);
extern int csp_println(void);
extern int csp_print_value(csp_rt_t* st, vtype_t vt, value_t val);

const char* csp_op_name(opcode_t op);
extern tok_t csp_opcode_to_tok(opcode_t opcode);
extern vtype_t csp_opcode_type(opcode_t opcode);
extern void csp_set_error(csp_rt_t*, csp_err_t);
extern void csp_clr_error(csp_rt_t*);
#if defined(SUPPORT_REACTIVE) && (SUPPORT_REACTIVE==1)
extern void csp_enq_elist(csp_rt_t* st, index_t x);
#endif

#ifdef __cplusplus
EXTERN_C_END
#endif

#endif
