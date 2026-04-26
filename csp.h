#ifndef __CSP_H__
#define __CSP_H__

#include <stdint.h>

#include "csp_config.h"

#ifndef EXTERN_C_BEGIN
#define EXTERN_C_BEGIN  extern "C" {
#define EXTERN_C_END    }
#endif

#ifdef __cplusplus
EXTERN_C_BEGIN
#endif
    
#define TAG_DECL  1
#define TAG_INSTR 0

//#define WORD_BITS    16
#define MOD_BITS     3     // (2^MOD_BITS-2) = 6 module instances
#define INSTR_BITS   7
#define INDEX_BITS   (MOD_BITS+(INSTR_BITS+1)) // max 512 elems
#define ANY_MOD      ((1 << MOD_BITS)-1)  // pattern for "any" mod
#define MAX_INDICES  (1 << INDEX_BITS)
#define MAX_INSTRS   64  // (less than (1<<ELEM_BITS) keep power of 2!!
#define MAX_DECLS    64  // (less than (1<<ELEM_BITS) keep power of 2!!
#define MAX_INPUTS   32
#define MAX_OUTPUTS  32
#define MAX_TIMERS   16
#define MAX_MODULES  16
#define MAX_MODS     32
#define MAX_QUEUE    (MAX_INSTRS)
#define MAX_INDEX    (MAX_INSTRS+1)
#define MAX_UNDO     (MAX_INSTRS)
#define MAX_STACK_DEPTH 4
#define STRING_BITS  9
#define NAME_BITS    5
#define MAX_STR_BUF  (1 << STRING_BITS) // total number of char in var names
#define MAX_NAME_LEN 8     // max var name len
#define MAX_ARGS     4     // max number of arguments to function
#define MAX_USER_FUNCS 16  // max user-defined functions

#define BAD_INDEX   (MAX_INDICES-1)
#define PARSE_ERROR BAD_INDEX

#define IS_MOD_INDEX(n) ((n) >= (1 << (INSTR_BITS+1)))
#define INDEX(n)  (((n)>>1) & ((1 << INSTR_BITS)-1))  // index in decl/instr
#define MOD(n)    ((n) >> (INSTR_BITS+1))
#define TAG(n)    ((n) & 1)
#define MAKE_INDEX(m,x,t) (((m)<<(INSTR_BITS+1)) | ((x)<<1) | (t))

#define MAX_PARSE_STACK_DEPTH 10
#define MAX_LINE_TOKENS 128

#define CSP_TRUE  -1  // all bits set, like openCL/Forth
#define CSP_FALSE 0

typedef uint16_t index_t;  // sizeof type >= INDEX_BITS

typedef enum {
    V_INTEGER,
    V_UNSIGNED,
    V_FLOAT,
    V_STRING
} vtype_t;

typedef enum {
    E_UNDEFINED,
    E_LITTLE,
    E_BIG,
} vendian_t;

typedef int32_t  ivalue_t;
typedef uint32_t uvalue_t;
typedef float    fvalue_t;
typedef int32_t  sindex_t;

typedef union {
    ivalue_t i;  // V_INTEGER
    uvalue_t u;  // V_UNSIGNED
    fvalue_t f;  // V_FLOAT
    sindex_t s;  // V_STRING (index into string buf)
} value_t;

// require csp_rt_init!
#define ZERO MAKE_INDEX(0,0,TAG_DECL)
#define ONE  MAKE_INDEX(0,1,TAG_DECL)

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
    ENTER,
    LEAVE,
    NEW,
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
    OP_NOP = 0,  // nothing
    OP_NOT,     // "!"  x=-y == x=0-y
    OP_INV,     // "~"  x=~y =  x=1^y        
    OP_NEG,     // "-"  x=-y == x=0-y
    OP_POS,     // "+"  x=+y == x=0+y
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
    OP_AND,     // "&"
    OP_OR,      // "|"
    OP_XOR,     // "^"
    OP_ANDAND,  // "&&"
    OP_OROR,    // "||"
    OP_EQ,      // "="
    OP_COMMA,   // ","
    OP_RULE,    // "?"

    // generate ops from MODULE/END
    OP_ENTER,   //
    OP_LEAVE,   //
    OP_NEW,     // #<module> <instance-name>

    // function call: y=func_index (low bit: 0=builtin, 1=user), z=arg or OP_COMMA
    OP_CALL,

    OP_LAST,
} opcode_t;

// Function flags
#define FUNC_IMMEDIATE  0x01  // can be called with > prefix
#define FUNC_PURE       0x02  // no side effects
#define FUNC_RAW_INDEX  0x04  // pass raw indices, not evaluated values

// Forward declarations
struct csp_rt;
struct csp_instr;

// Function pointer types
// Normal: args are pre-evaluated into value_t array
typedef ivalue_t (*csp_func_fn)(struct csp_rt* st, value_t* args, uint8_t nargs);
// Raw: receives instruction pointer to access raw indices (for print, timeout)
typedef ivalue_t (*csp_func_raw_fn)(struct csp_rt* st, struct csp_instr* ip);

// Function table entry
typedef struct {
    const char* name;
    uint8_t namelen;
    uint8_t nargs;      // number of arguments (0-4)
    uint8_t flags;      // FUNC_RAW_INDEX| FUNC_IMMEDIATE | FUNC_PURE
    csp_func_fn fn;     // function to call
} csp_func_t;

typedef enum {
    DECL_MODULE,   // 'module'
    DECL_END,      // 'end'
    DECL_CONSTANT, // 'constant'
    DECL_VARIABLE, // 'variable'
    DECL_DIGITAL,  // 'digital'
    DECL_ANALOG,   // 'analog'
    DECL_TIMER,    // 'timer'
    DECL_CAN,      // 'can'
    DECL_UART,     // 'uart'
    DECL_SOCKET,   // 'socket'
    DECL_MOD,      // module instance
} decl_t;

#define IS_DECL(i)  (TAG((i)) == TAG_DECL)
#define IS_INSTR(i)  (TAG((i)) == TAG_INSTR)

#define DECL_TYPE(s,i) ((s)->decl[(i)].type)
#define IS_QVAR(s,i)   (DECL_TYPE((s),(i))==DECL_VARIABLE)
#define IS_CONST(s,i)  (DECL_TYPE((s),(i))==DECL_CONSTANT)
#define IS_MODULE(s,i) (DECL_TYPE((s),(i))==DECL_MODULE)
#define IS_MOD(s,i)    (DECL_TYPE((s),(i))==DECL_MOD)
#define IS_END(s,i)    (DECL_TYPE((s),(i))==DECL_END)
#define IS_CAN(s,i)    (DECL_TYPE((s),(i))==DECL_CAN)


#define OP(s,i) ((s)->instr[(i)].op)
#define IS_ENTER(s,i) (OP((s),(i))==OP_ENTER)
#define IS_LEAVE(s,i) (OP((s),(i))==OP_LEAVE)
#define IS_COND(s,i)   ((s)->instr[(i)].cond)


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
    index_t  mx;         // module index
    index_t  iq;         // index in mod table
} csp_mod_t;

typedef struct PACKED  { // 32
    value_t init;    // init value
} csp_variable_t;

typedef struct PACKED  { // 32
    value_t init;   // constant value
} csp_constant_t;

typedef struct PACKED  { // 18
    unsigned pin:8;
    unsigned port:8;
    unsigned pullup:1;
    unsigned pulldown:1;
    // init?
} csp_digital_t;

typedef struct PACKED { // 17
    unsigned pin:8;
    unsigned port:8;
    unsigned pwm:1;    // pwmoutput
    // init?    
} csp_analog_t;

typedef struct PACKED {  // 27 = 10+10+5  (29=12+12+5)
    unsigned id:INDEX_BITS; // variable | constant (unsigned) 11/29 bit
    unsigned endian:2; // |little|big
    unsigned bit:9;   // 0-511   // bit start pos
    unsigned len:5;   // (1-32)  // data length -1
} csp_can_t;

typedef struct PACKED {     // 22 = 1+1+10+10 (25=1+12+12)
    unsigned running:1;     // != 0 if timer is running
    unsigned init:1;        // initial value if given
    unsigned px:INDEX_BITS; // variable | constant (unsigned)
    unsigned tx:INDEX_BITS; // start time tick (intern variable)
} csp_timer_t;

typedef struct PACKED csp_instr { // 6+2+1+11+11 = 9+22 = 31
    opcode_t op:6;          // OP_xxx
    unsigned vt:2;          // value type (x)
    unsigned cond:1;        // conditional instruction
    unsigned y:INDEX_BITS;  // src1  (instruction/decl)
    unsigned z:INDEX_BITS;  // src2  (instruction/decl)
} csp_instr_t;

typedef struct PACKED {  // 57 = 6 + 51
    decl_t type:6;                 // DECL_xxx
    unsigned name:STRING_BITS;     // string index
    unsigned vt:2;                 // value type
    unsigned res:5;                // 1-32
    unsigned in:1;                 // input leaf
    unsigned out:1;                // output leaf
    union PACKED {  // 32
	csp_module_t   md;  // 16
	csp_mod_t      mq;  // 24 = 12+12
	csp_variable_t va;  // 32
	csp_constant_t cn;  // 32
	csp_digital_t  di;  // 18		
	csp_analog_t   an;  // 17
	csp_can_t      ca;  // 29 (25)
	csp_timer_t    tm;  // 25 (22)
    };
} csp_decl_t;

typedef struct csp_rt
{
    csp_instr_t instr[MAX_INSTRS];  // instructions used
    csp_decl_t decl[MAX_DECLS];    // declarations used    
    value_t xval[MAX_INDEX];        // instruction xvalue
    value_t dval[MAX_INDEX];        // declaration value
    index_t input[MAX_INPUTS];   // list of inputs (digital/analog ...)
    index_t output[MAX_OUTPUTS]; // list of outputs (digital/analog ...)
    index_t module[MAX_MODULES]; // list of modules
    index_t mod[MAX_MODS];       // list of mods    
    index_t timer[MAX_TIMERS];   // list of timers
    index_t mofs[MAX_MODS];      // offset in state given mod
    char    str[MAX_STR_BUF];    // store variable names
    int esp;  // eval stack pointer
    struct PACKED { index_t ix; index_t so; unsigned iq:MOD_BITS; }
	stack[MAX_STACK_DEPTH];
    unsigned transaction:1;      // 1 if keeping a log
    unsigned reactive:1;         // 1 if push backedges to queue
    unsigned cond:1;             // 1 if mark node as conditional
    uint32_t line;               // line number when parsing
    uint32_t err;                // error code
    index_t nn;                  // number of instructions
    index_t nd;                  // number of decls
    index_t nt;                  // number of timers
    index_t ni;                  // number of input
    index_t no;                  // number of output
    index_t nm;                  // number of modules
    index_t nq;                  // number of mods (instances of modules)
    uint32_t strp;               // string position
    index_t mdef;                // module being defined
    index_t ent;                 // entry op of module
    index_t so;                  // state offsets for mods
    index_t iq;
    // during eval
    uint32_t update;             // update counter
    uint32_t wait_ms;            // sleep time or NOTIMEOUT
#ifdef WANT_REACTIVE
    bitset_decl(inq, MAX_INDEX); // mark nodes in queue during eval    
    index_t queue[MAX_QUEUE];    // nodes in queue
    int hd,tl;  // queue head and tail
    // back references
    index_t idg[MAX_INDEX];    // in degree per instr
    index_t ofs [MAX_INDEX+1]; // output offset from each instr
    index_t edg [MAX_INDEX+1]; // edg[ofs[n]+0...ideg[n]-1] back pointer
#endif

#ifdef WANT_TRANSACTION
    int up;  // undo pointer
    struct { index_t x; value_t v; } undo[MAX_UNDO];
#endif
    bitset_decl(xset, MAX_INDEX); // mark instr updated during cycle
    bitset_decl(dset, MAX_INDEX); // mark decl updated during cycle
    // check if any node has been set: anyx|anyd == CSP_TRUE
    int8_t  anyx;  // CSP_TRUE|CSP_FALSE
    int8_t  anyd;  // CSP_TRUE|CSP_FALSE
    uint32_t cycle;
#ifdef WANT_STATISTICS
    uint32_t num_eval0;
#endif
    // user-defined functions (checked before builtin)
    const csp_func_t* user_funcs;
    uint8_t num_user_funcs;
} csp_rt_t;

// Built-in function table (defined in csp_rt.c)
extern const csp_func_t csp_builtin_funcs[];
extern const uint8_t csp_num_builtin_funcs;

// n = |mod|index|t|
// if (m == *) m = st->iq -- set current mod
// if (m >= 1) {
//   if (decl)
//      i = INDEX(n) - decl[INDEX(n)].mq.mx
//      i += st->mofs[m];
// }
// 

static inline int st_index(csp_rt_t* st, index_t n)
{
    if (IS_MOD_INDEX(n)) {
	int m = MOD(n); // extract module index
	int i;
	index_t md, mx;
	if (m == ANY_MOD)
	    m = st->iq;
	md = st->mod[m];
	mx = st->decl[INDEX(md)].mq.mx;
	if (IS_DECL(n)) {
	    i = INDEX(n) - INDEX(mx);
	}
	else {
	    i = n - st->decl[INDEX(mx)].md.ent;
	}
	return i + st->mofs[m];
    }
    return (n >> 1); // since mod=0 just shift tag bit
}

#ifdef WANT_REACTIVE
// enq an node for recalculation
static inline void csp_enq(csp_rt_t* st, index_t x)
{
    if (bitset_tst(st->inq,x))
	return;
#if defined(CSP_DEBUG) && !defined(CSP_EMBEDDED)
    printf("enq: %d\n", x);
#endif
    if ((st->tl - st->hd) != MAX_QUEUE) {
	st->queue[st->tl % MAX_QUEUE] = x;
	st->tl++;
	bitset_set(st->inq, x);
    }
}

// enq all nodes i nodes (back)edge list
static inline void csp_enq_elist(csp_rt_t* st, index_t x)
{
    int i;
    index_t ix = INDEX(x);
    for (i = 0; i < st->idg[ix]; i++) {
	index_t p = st->edg[st->ofs[ix]+i];  // parent node
	if (MOD(p) == ANY_MOD) {
	    p = MAKE_INDEX(st->iq,INDEX(p),TAG_INSTR);
	}
	csp_enq(st, p);
    }
}

static inline index_t csp_deq(csp_rt_t* st)
{
    index_t x;
    if (st->tl == st->hd)
	return BAD_INDEX;
    x = st->queue[st->hd % MAX_QUEUE];
    st->hd++;
    bitset_clr(st->inq,x);  // keep? if eval once per cycle
#if defined(CSP_DEBUG) && !defined(CSP_EMBEDDED)
    printf("deq: %d\n", x);
#endif
    return x;
}
#endif

static inline value_t csp_value(csp_rt_t* st, index_t n)
{
    int i = st_index(st, n);
    if (IS_INSTR(n))
	return st->xval[i];
    else
	return st->dval[i];
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

static inline void csp_set_xvalue(csp_rt_t* st, index_t n, value_t v)
{
    int i = st_index(st, n);
    value_t cv = st->xval[i];
    if (v.u != cv.u) {
#ifdef WANT_TRANSACTION
	if (st->transaction) {
	    if (!bitset_tst(st->xset,i)) { // push to undo queue
		st->undo[st->up].x = n;
		st->undo[st->up].v = cv;
		st->up++;
	    }
	}
#endif
	bitset_set(st->xset,i);
	st->anyx = CSP_TRUE;
#ifdef WANT_REACTIVE
	if (st->reactive)
	    csp_enq_elist(st,n);
#endif
	st->xval[i] = v;
	st->update++;
    }    
}

// set value on declaration node (variable/digital/analog ...)
static inline void csp_set_dvalue(csp_rt_t* st, index_t n, value_t v)
{
    int i = st_index(st, n);
    value_t cv = st->dval[i];
    if (v.u != cv.u) {
#ifdef WANT_TRANSACTION
	if (st->transaction) {
	    if (!bitset_tst(st->dset,i)) { // push to undo queue
		st->undo[st->up].x = n;
		st->undo[st->up].v = cv;
		st->up++;
	    }
	}
#endif
	bitset_set(st->dset, i);
	st->anyd = CSP_TRUE;	
#ifdef WANT_REACTIVE
	if (st->reactive)
	    csp_enq_elist(st,n);
#endif
	st->dval[i] = v;
	st->update++;
    }    
}

static inline void csp_set_value(csp_rt_t* st, index_t n, value_t v)
{
    if (IS_INSTR(n))
	csp_set_xvalue(st, n, v);
    else
	csp_set_dvalue(st, n, v);
}

static inline void csp_set_ivalue(csp_rt_t* st, index_t n, ivalue_t v)
{
    value_t vv;
    vv.i = v;
    csp_set_value(st, n, vv);
}

static inline void csp_set_fvalue(csp_rt_t* st, index_t n, fvalue_t v)
{
    value_t vv;
    vv.f = v;
    csp_set_value(st, n, vv);
}

extern void    csp_rt_init(csp_rt_t*);
extern void    csp_rt_start(csp_rt_t*);
extern void    csp_set_user_funcs(csp_rt_t*, const csp_func_t*, uint8_t);
extern int     csp_lookup_func(csp_rt_t*, const char*, uint8_t);
extern int     csp_set_transaction(csp_rt_t*, int onoff);
extern int     csp_set_reactive(csp_rt_t*, int onoff);
extern int     csp_parse(csp_rt_t*, char* str);
extern void    csp_csr(csp_rt_t* st);
extern index_t csp_eval(csp_rt_t* st);
extern int     csp_eval0(csp_rt_t* st, int);
extern index_t csp_react(csp_rt_t* st);
extern void    csp_undo(csp_rt_t* st);
extern void    csp_commit(csp_rt_t* st);

extern void    csp_state_init(csp_rt_t* st);
//
extern index_t csp_new_decl(csp_rt_t* st, char* name, int name_len, decl_t op);
extern index_t csp_lookup_decl(csp_rt_t* st, char* module, char* name);

// backend port (linux/arduino/LPCopen/FreeRTOS)
extern uint32_t csp_time_ms(void);
extern unsigned long csp_time_us(void);
extern void csp_setup(csp_rt_t* st);
extern void csp_input(csp_rt_t* st);
extern void csp_output(csp_rt_t* st);

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

#ifdef __cplusplus
EXTERN_C_END
#endif

#endif
