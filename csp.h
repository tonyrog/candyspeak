#ifndef __CSP_H__
#define __CSP_H__

#include <stdint.h>
#include <stdlib.h>

#include "csp_config.h"

// Prevent inlining of large parse functions to reduce code size
#ifdef __GNUC__
#define NOINLINE __attribute__((noinline))
#else
#define NOINLINE
#endif

#ifndef EXTERN_C_BEGIN
#define EXTERN_C_BEGIN  extern "C" {
#define EXTERN_C_END    }
#endif

#ifdef __cplusplus
EXTERN_C_BEGIN
#endif

#ifdef DEBUG
#include <stdio.h>
extern int debug;
#define DBG(...) do { \
	if (debug) printf(__VA_ARGS__);		\
    } while(0)
#else
#define DBG(...)
#endif


#define PACKED __attribute__((packed))

// an index has the following structure
// obj:4, index:12   // declaration object
//
// obj is current object 0 = global, cur = 2^OBJ_BITS-1 or the actual obj index
// tag is TAG_DECL (offs[m]+st->decl[index])
// 

typedef uint16_t index_t;  // sizeof type >= INDEX_BITS
typedef uint8_t  reg_t;    // at most 256 registers
typedef unsigned bool_t;

#define PORT_BITS 4   // port 0..15    mega is 11 ports, with 8 pins each
#define PIN_BITS  7   // pin  0..127
#define BUF_BITS  6   // buffer index

#if defined(__AVR__)
#include <avr/pgmspace.h>
#define RODATA          PROGMEM
#define ro_byte(p)      pgm_read_byte((p))
#define ro_word(p)      pgm_read_word((p))
#define ro_ptr(p)       (void *)pgm_read_word((p))
#define ro_memcmp(a,b,n) memcmp_P((a), (b), (n))
#define DECL_BITS    4
#define INSTR_BITS   4
#define OBJ_BITS     3
#define STRING_BITS  7
#else
#define RODATA
#define ro_byte(p)      (*(p))
#define ro_word(p)      (*(p))
#define ro_ptr(p)       (*(p))
#define ro_memcmp(a,b,n) memcmp((a), (b), (n))
#define DECL_BITS    9
#define INSTR_BITS   8
#define OBJ_BITS     4
#define STRING_BITS  9
#endif

typedef const char rochar;  // PROGMEM string character type

#define INDEX_BITS   (OBJ_BITS+DECL_BITS)
#define REG_BITS     4   // r0..r15
#define FUNC_BITS    5   // 0..31 (need more!)
#define PART_BITS    4
#define GLOBAL       0                       // global level
#define CURRENT      ((1 << OBJ_BITS)-1)     // current obj
#define MAX_INDICES  (1 << INDEX_BITS)
#define MAX_REGS     (1 << REG_BITS)
#define MAX_INSTRS   (1 << INSTR_BITS)
#define MAX_DECLS    (1 << DECL_BITS)
#if defined(__AVR__)
#define MAX_INPUTS   8   // Uno has ~20 pins total
#define MAX_OUTPUTS  8
#define MAX_TIMERS   4
#define MAX_HEAP     64  // buffer heap bytes
#else
#define MAX_INPUTS   32  // <= then MAX_DECLS
#define MAX_OUTPUTS  32  // <= then MAX_DECLS
#define MAX_TIMERS   16  // <= then MAX_DECLS
#define MAX_HEAP     1024 // buffer heap bytes
#endif
#define MAX_VARREFS  MAX_TIMERS
#define MAX_MODULES  (1 << OBJ_BITS)
#define MAX_OBJECTS  (1 << OBJ_BITS)
#define MAX_BUFS     (1 << BUF_BITS)
#define MAX_QUEUE    (MAX_INSTRS)
#define MAX_INDEX    (MAX_INSTRS+1)
#define MAX_QENTRY   (1 << (OBJ_BITS + INSTR_BITS)) // packed (obj,ip) key space
#define DIR_BITS 2
#define TYPE_BITS 4  // supports up to 15 types & objects
#define ENDIAN_BITS 2

// Queue entry: pack obj and ip together
#define MAKE_QENTRY(obj, ip)  (((obj) << INSTR_BITS) | (ip))
#define QENTRY_OBJ(e)         ((e) >> INSTR_BITS)
#define QENTRY_IP(e)          ((e) & ((1 << INSTR_BITS) - 1))
#define MAX_STACK_DEPTH 4
#define NAME_BITS    5
#define MAX_STR_BUF  (1 << STRING_BITS) // total number of char in var names
#define MAX_NAME_LEN 31    // max var name len
#define MAX_ARGS     4     // max number of arguments to function

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
    // object types type that can be use for builtin functions
    V_TIMER    = 8,
    V_DIGITAL  = 9,
    V_ANALOG   = 10,
    V_CAN      = 11,    
} vtype_t;

// create argument type bitmask
#define MAKE_TYPE0()            0
#define MAKE_TYPE1(t0)          (t0)
#define MAKE_TYPE2(t0,t1)       ((t0)|((t1)<<4))
#define MAKE_TYPE3(t0,t1,t2)    ((t0)|((t1)<<4)|((t2)<<8))
#define MAKE_TYPE4(t0,t1,t2,t3) ((t0)|((t1)<<4)|((t2)<<8)|((t3)<<12))

typedef enum {
    E_NATIVE    = 0x00,    
    E_LITTLE    = 0x01,
    E_BIG       = 0x02,
    E_UNDEFINED = 0x03,
} vendian_t;

typedef int32_t  ivalue_t;
typedef uint32_t uvalue_t;
typedef int32_t  sindex_t;

typedef struct PACKED {
    unsigned long period:28; // timeout value ms (74h max)
    unsigned _res:1;         // reserved (auto restart?)
    unsigned fired:1;        // timeout occurred this cycle (edge-triggered)
    unsigned running:1;      // timer is runnig (tx is valid time)
    unsigned val:1;          // one bit value 1 = start, 0 = stop
} tvalue_t;

typedef struct PACKED {
    unsigned pin:PIN_BITS;
    unsigned port:PORT_BITS;
    unsigned dir:DIR_BITS;
    unsigned pullup:1;
    unsigned pulldown:1;
    unsigned val:16;    // we may shift in bits...?
} dvalue_t;

typedef struct PACKED {
    unsigned pin:PIN_BITS;
    unsigned port:PORT_BITS;
    unsigned dir:DIR_BITS;    
    unsigned pwm:1;
    unsigned endian:2; // |little|big interpretation    
    unsigned val:16;
} avalue_t;

typedef enum  {
    PART_VAL=0,
    PART_PIN,
    PART_PORT,
    PART_DIR,
    PART_PWM,
    PART_ENDIAN,
    PART_PULLUP,
    PART_PULLDOWN,
    PART_PERIOD,
    PART_FIRED,
    PART_ID,
    PART_LAST,
} csp_part_t;

// How to reach a leaf's value. Everything lives in the buffer heap.
// See doc/DESCRIPTORS.md.
typedef enum {
    VIEW_SLOT = 0,   // value_t struct stored in its buffer (config+value types)
    VIEW_HEAP = 1,   // bit-field in a buffer (scalar variables, buffer views)
} view_kind_t;

#define VIEW_F_SIMPLE 0x01   // covers whole buffer, byte aligned, native endian
#define VIEW_F_GLOBAL 0x02   // buf id is global (not object-offset)

// One per leaf index_t (indexed by st_index).
typedef struct {
    uint8_t  kind;   // view_kind_t
    uint8_t  vt;     // value type (vtype_t); SLOT reads vt from decl instead
    uint8_t  buf;    // buffer id (both kinds)
    uint8_t  pos;    // VIEW_HEAP: start bit in buffer
    uint8_t  len;    // VIEW_HEAP: number of bits - 1
    uint8_t  endian; // VIEW_HEAP: little/big
    uint8_t  flags;  // VIEW_HEAP: VIEW_F_*
} csp_view_t;

// One per unique buffer. RAM table, filled at start.
typedef struct {
    uint16_t hp;        // heap byte offset
    uint8_t  nbytes;    // size in bytes
    uint8_t  loc;       // RAM/ROM/IO
    uint8_t  transport; // none/pin/can
    uint8_t  dir;       // in/out
    uint32_t xref;      // pin-number / can-id
} csp_buf_t;

#if defined(USE_FIXPOINT) && (USE_FIXPOINT == 1)
#include "csp_fixpoint.h"
typedef fixpoint_t fvalue_t;
#define FVALUE_IS_FIXPOINT 1
extern int csp_print_fixpoint(fvalue_t v);
#else
typedef float fvalue_t;
#define FVALUE_IS_FIXPOINT 0
#endif

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
#define op_FMOV(y)     (y)
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
#define op_FMOV(y)     (y)
#define op_CVTIF(y)    ((fvalue_t)(y))
#define op_CVTFI(y)    ((ivalue_t)(y))
#endif

#define op_NOT(y)  (~BOOL((y)))
#define op_NEG(y)  (-(y))
#define op_MOV(y)  (y)
#define op_BNOT(y)  (~(y))

typedef union {
    ivalue_t i;  // V_INTEGER
    uvalue_t u;  // V_UNSIGNED
    fvalue_t f;  // V_FLOAT
    sindex_t s;  // V_STRING (index into string buf)
    tvalue_t t;  // V_TIMER
    dvalue_t d;  // V_DIGITAL
    avalue_t a;  // V_ANALOG
} value_t;

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
    NEWLINE,   // \n \r \r\n
    LP,        // "("
    RP,        // ")"
    COLON,     // ":"
    HASH,      // "#"
    DOT,       // "."
    LB,        // "["
    RB,        // "]"
    INT,       // 123 | 0x9ab
    FLT,       // 0.123
    STR,       // "abc"
    EXCLAMATION, // "!"  x=-y == x=0-y
    TILDE,       // "~"  x=~y =  x=1^y        
    MINUS1,      // "-"  x=-y == x=0-y
    PLUS1,       // "+"  x=+y == x=0+y
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
    RIMP,    // "<-"    
    COMMA,   // ","
    // query rule/operator
    QUEST,   // "?"
    WORD,       // abc
    // option keywords
    PULLUP,     // 'pullup'
    PULLDOWN,   // 'pulldown'
    RESOLUTION, // 'resolution'
    IN,         // 'in'
    OUT,        // 'out'
    INOUT,      // 'inout'
    T_PWM,        // 'pwm'
    FLOAT,      // 'float'
    INTEGER,    // 'integer'
    UNSIGNED,   // 'unsigned'
    STRING,     // 'string'
    NATIVE,     // 'native'
    LITTLE,     // 'little'
    BIG,        // 'big'
    T_LAST,     // number of enumerated tokens
} tok_t;

typedef enum {
    D_NONE = 0,
    D_MODULE,   // 'module'
    D_END,      // 'end'
    D_STATES,   // 'states'
    D_IN,       // 'in'
    D_CONSTANT, // 'constant'
    D_VARIABLE, // 'variable'
    D_DIGITAL,  // 'digital'
    D_ANALOG,   // 'analog'
    D_TIMER,    // 'timer'
    D_CAN,      // 'can'
    D_BUFFER,   // 'buffer'
    D_UART,     // 'uart'
    D_SOCKET,   // 'socket'
    D_MOD,      // module instance
    D_LAST,     // number of enumerated declarations
} dtok_t;

typedef struct {
    char* ptr;
    int len;
} tstr_t;

typedef union
{
    tstr_t str;
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
    OP_MOV,     // "mov" x=y == x=y
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
    OP_FMOV,     // "mov"  x=y
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
    OP_RIMP,    // "<-"    
    OP_COMMA,   // ","
    // rule
    OP_RULE,    // "?"
    OP_NEXT,    // "next"

    OP_ENTER,   // enter object
    OP_LEAVE,   // leave object
    OP_NEW,     // #<module> <instance-name>
    OP_LD,      // load register from memory
    OP_LDP,     // load register from memory part
    OP_ST,      // store register to memory
    OP_STP,     // store register to memory part
    OP_STIMP,   // store for <- (reactive assign), same as ST but marks rimp
    OP_CHG,     // r |= dset[ix], check if variable changed
    OP_LI,      // load signed 16-bit constant
    OP_LIU,     // load unsigned 16-bit constant (zero extend)
    OP_LIH,     // load high 16-bit (OR into high bits)
    OP_ARG,     // load argument from register
    OP_CALL,    // function call:
    OP_EQI,     // compare memory with 8 bit value, result in x
    OP_STI,     // store immediate value to memory (mirror of EQI)
    OP_INSTATE, // #in <state> block gate: if reg != state, skip block (nxt)
    OP_LAST,
} opcode_t;


// Forward declarations
struct _csp_rt_t;
struct csp_instr;

// 6 bits may be used to describe declaration type
// but decl type from 8-15 are also used as object types
typedef enum {
    DECL_NONE=0,            // emtpy declaration
    DECL_VARIABLE=1,        // 'variable'
    DECL_CONSTANT=2,        // 'constant'
    DECL_MODULE=3,          // 'module'
    DECL_END=4,             // 'end'
    DECL_OBJECT=5,          // module instance
    DECL_STATES=6,
    DECL_IN=7,
    
    // 8-15
    DECL_TIMER=V_TIMER,     // 'timer'
    DECL_DIGITAL=V_DIGITAL, // 'digital'
    DECL_ANALOG=V_ANALOG,   // 'analog'
    DECL_CAN=V_CAN,         // 'can'
    DECL_BUFFER=12,         // 'buffer' (heap-backed storage)
    DECL_VIEW=13,           // synthetic bit/byte view into a buffer (Buf[a..b])
} decl_t;

#define DECL_TYPE(s,i) (decl((s),(i),type))
#define IS_CONST(s,i)  (DECL_TYPE((s),(i))==DECL_CONSTANT)
#define IS_CAN(s,i)    (DECL_TYPE((s),(i))==DECL_CAN)

#define MAKE_RES(r) ((r)-1)
#define GET_RES(rr) ((rr)+1)

#define MAKE_CAN_LEN(len) ((len)-1)
#define GET_CAN_LEN(len) ((len)+1)

#define NOTIMEOUT 0xffffffff

typedef struct PACKED {
    rochar*  name;     // token name (RODATA)
    uint8_t namelen;
    uint8_t  tok;
    int8_t   code;
    int8_t   arity;
    int8_t   prec;
    int8_t   assoc;
} op_entry_t;

extern const op_entry_t tok_table[] RODATA;
extern const op_entry_t decl_table[] RODATA;

typedef struct PACKED {
    rochar*  name;     // opcode name (RODATA)
    uint8_t  tok;      // token that match the op
    int8_t arity;      // number of args
    uint8_t rtype;     // return type
    uint8_t _res;      // reserved
    uint16_t argtypes; // instruction argument types
} op_info_t;

extern const op_info_t op_info[] RODATA;


// new instruction format
// general operations OP_ADD ...

#define INSTR_COMMON \
        opcode_t op:6

typedef struct PACKED {
    INSTR_COMMON;
    unsigned x:REG_BITS;
    unsigned y:REG_BITS;
    unsigned z:REG_BITS;
} csp_instr_alu_t;

// op = ST | LD | STP | LDP?
// load or store register from memory
//
//   x = mem[y]
//   x = mem[y,z]
//   x = mem[part]
// 
typedef struct PACKED {
    INSTR_COMMON;
    unsigned x:REG_BITS;      // destination register
    unsigned y:REG_BITS;      // y register when pos, y imm when part (STP)
    unsigned z:REG_BITS;      // len register
    unsigned mem:INDEX_BITS;  // declaration: variable/constant
} csp_instr_mem_t;

// op EQI - compare 8 bit immediate with memory and store in x
typedef struct PACKED {
    INSTR_COMMON;
    unsigned x:REG_BITS;      // destination register
    signed imm:8;             // y register when pos, y imm when part (STP)
    unsigned mem:INDEX_BITS;  // declaration: variable/constant
} csp_instr_memi_t;

// op LI / ARG
// load immediate LI load small 16 bit signed constant
typedef struct PACKED {
    INSTR_COMMON;
    unsigned x:REG_BITS;
    signed imm:16;
} csp_instr_imm_t;

typedef struct PACKED {
    INSTR_COMMON;    
    unsigned cnd:REG_BITS; // condition register
    int16_t nxt;           // relative jump if !cnd
} csp_instr_rule_t;

typedef struct PACKED {
    INSTR_COMMON;
    unsigned x:REG_BITS;   // body result
} csp_instr_next_t;

// op INSTATE - #in <state> block gate. A LD of the state variable precedes it;
// if that register != imm, jump nxt to skip the whole block (sequential path).
typedef struct PACKED {
    INSTR_COMMON;
    unsigned x:REG_BITS;   // register holding the current State value
    signed   imm:8;        // target state number
    signed   nxt:14;       // relative jump to skip the block if x != imm
} csp_instr_instate_t;

typedef struct PACKED {
    INSTR_COMMON;
    unsigned num:INSTR_BITS;  // number of instructions
    index_t  mx;     // module index
} csp_instr_enter_t;

typedef struct PACKED {
    INSTR_COMMON;
    unsigned num:INSTR_BITS;  // number of instructions
    index_t  mx;     // module index
} csp_instr_leave_t;

typedef struct PACKED {
    INSTR_COMMON;
    unsigned ent:INSTR_BITS; // entry point index in instr[]
    index_t  obj;            // object declaration index 
} csp_instr_new_t;

typedef struct PACKED {
    INSTR_COMMON;
    unsigned x:REG_BITS;     // result register
    unsigned idx:FUNC_BITS;  // function index
    unsigned usr:1;          // user function
    unsigned avt:16;         // argument value types 4 bit per argument
} csp_instr_call_t;

typedef union {
    // uint32 need on arduino uno (unsigned is 16 bit?)
    struct PACKED { INSTR_COMMON; uint32_t rest:26; };
    csp_instr_enter_t e;
    csp_instr_leave_t v;
    csp_instr_new_t n;
    csp_instr_imm_t i;
    csp_instr_mem_t m;
    csp_instr_memi_t mi;
    csp_instr_call_t f;
    csp_instr_rule_t r;
    csp_instr_next_t x;
    csp_instr_instate_t in;
    csp_instr_alu_t a;
} csp_instr_t;

typedef enum {
    DIR_NONE  = 0x00,
    DIR_IN    = 0x01,
    DIR_OUT   = 0x02,
    DIR_INOUT = 0x03
} pindir_t;

#define DECL_COMMON \
    decl_t type:6; \
    pindir_t dir:DIR_BITS; \
    unsigned name:STRING_BITS; \
    unsigned vt:TYPE_BITS; \
    unsigned res:5; \
    unsigned is_mapped:1; \
    unsigned bound:1; \
    unsigned reg:REG_BITS

typedef struct PACKED {
    DECL_COMMON;
    index_t n;          // number of nodes in module definition
    index_t ent;        // entry point in instr
} csp_module_t;

typedef struct PACKED {
    DECL_COMMON;    
    index_t  mx;           // module declaration index
    unsigned m:OBJ_BITS;   // index in object table
} csp_object_t;

typedef struct PACKED  {
    DECL_COMMON;    
    value_t init;    // init value
} csp_variable_t;

typedef struct PACKED  {
    DECL_COMMON;    
    value_t init;   // constant value
} csp_constant_t;

typedef struct PACKED  {
    DECL_COMMON;    
    unsigned pin:PIN_BITS;
    unsigned port:PORT_BITS;
    unsigned pullup:1;
    unsigned pulldown:1;
} csp_digital_t;

typedef struct PACKED {
    DECL_COMMON;
    unsigned pin:PIN_BITS;
    unsigned port:PORT_BITS;
    unsigned pwm:1;    // pwm output
    unsigned endian:2; // |little|big
} csp_analog_t;

typedef struct PACKED {
    DECL_COMMON;    
    unsigned id:INDEX_BITS; // variable | constant (unsigned) 11/29 bit
    unsigned endian:2; // |little|big
    unsigned bit:9;   // 0-511   // bit start pos
    unsigned len:5;   // (1-32)  // data length -1
} csp_can_t;

typedef struct PACKED {
    DECL_COMMON;    
    unsigned long period:28; // timeout value ms (74h max)
    unsigned _res:1;         // reserved    
    unsigned fired:1;        // timeout occurred this cycle (edge-triggered)
    unsigned running:1;      // timer is runnig (tx is valid time)
    unsigned init:1;         // one bit value 1 = start, 0 = stop
} csp_timer_t;

    
typedef union {
    struct PACKED { DECL_COMMON; };
    csp_module_t   md;
    csp_object_t   mq;
    csp_variable_t va;
    csp_constant_t cn;
    csp_digital_t  di;
    csp_analog_t   an;
    csp_can_t      ca;
    csp_timer_t    tm;
} csp_decl_t;

typedef enum {
    ERR_OK = 0,
    ERR_SYNTAX,
    ERR_TOO_MANY_TOKENS,
    ERR_STRING_SPACE_EXHUSTED,    
    ERR_TOO_MANY_DECLARATIONS,
    ERR_TOO_MANY_INSTRUCTIONS,
    ERR_TOO_MANY_OBJECTS,
    ERR_MODULE_NOT_DECLARED,
    ERR_TOO_MANY_STATES,    
    ERR_STATE_NOT_DECLARED,
    ERR_END_MISMATCH,
    ERR_NOT_A_MODULE,
    ERR_OBJECT_NOT_DECLARED,
    ERR_VARIABLE_NOT_DECLARED,
    ERR_FIELD_NOT_FOUND,
    ERR_FUNCTION_DOES_NOT_EXIST,
    ERR_INTERNAL_ERROR,
    ERR_FUNCTION_ARGUMENT_TYPE_MISMATCH,
    ERR_ALREADY_DEFINED,
    ERR_NAME_TOO_LONG,
} csp_err_t;

// parser state, save state before parse
// so that restore may be possible when error
typedef struct PACKED {
    index_t nn;                  // number of instructions
    index_t nd;                  // number of decls
    index_t nq;                  // number of objects
    index_t ns;                  // number of states
    uint32_t strp;               // string table position (grows up)
    uint32_t err_strp;           // error string position (grows down from MAX_STR_BUF)
    csp_err_t err;               // error code
    uintptr_t err_args[3];       // error arguments for printf
    uint32_t line;               // line number when parsing
} csp_pstate_t;

// Function pointer types
typedef value_t (*csp_func_fn)(struct _csp_rt_t* st, uint16_t type,
			       value_t* args, uint8_t nargs);

typedef int (*csp_const_fn)(struct _csp_rt_t* st, const char* name, int len,
			    value_t*, vtype_t*);

// csp_func_t.flags bits (packed; a whole byte so ro_byte(&e.flags) works)
#define FUNC_PURE   0x01        // side-effect free
#define FUNC_RONAME 0x02        // .name points to a rochar (RODATA) string

// Is a func table in ROM? Builtin is compile-time (RODATA on AVR); user funcs
// pass a flag to csp_set_ufuncs. On the host RODATA==RAM so this is always 0
// and the rom-aware readers collapse to plain access.
#if defined(__AVR__)
#define BUILTIN_ROM 1
#else
#define BUILTIN_ROM 0
#endif

// Function table entry
typedef struct {  // not packed?
    const char* name;
    uint8_t namelen;
    uint8_t arity;              // number of arguments (0-4)
    uint8_t flags;              // FUNC_PURE | FUNC_RONAME
    uint8_t rtype;              // return type
    uint16_t argtypes;          // argument types MAKE_TYPEx
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

typedef enum { DIN = 0, DOUT = 1 } dio_t;

#define NUM_BITS (16-STRING_BITS)
typedef struct
{
    unsigned name:STRING_BITS;     // string index
    unsigned snum:NUM_BITS;        // state number
} state_t;

typedef struct _csp_rt_t
{
    value_t reg[MAX_REGS];         // register area
    value_t arg[MAX_ARGS];         // loaded before call

    // experiments and test in RAM
    csp_instr_t ram_instr[MAX_INSTRS+1]; // instructions used (one dummy slot!)
    csp_decl_t  ram_decl[MAX_DECLS];     // declarations used
    char        ram_str[MAX_STR_BUF];    // store variable names

    // All leaf values live in the buffer heap (see doc/DESCRIPTORS.md).
    csp_view_t view[MAX_INDEX];   // per-leaf view descriptor
    csp_buf_t  buf[MAX_BUFS];     // buffer table
    index_t    nbuf;              // number of buffers allocated
    // The transaction model is permanent: rules read the committed DIN heap and
    // write the DOUT shadow; csp_commit copies dirty leaves DOUT->DIN. So a cycle
    // never sees its own writes -> sequential and reactive yield the same state.
    uint8_t*   heap[2];           // heap[DIN]=heap0, heap[DOUT]=heap1
    // 4-aligned so VIEW_SLOT value_t access into the heap is aligned
    uint8_t    heap0[MAX_HEAP] __attribute__((aligned(4)));  // committed (DIN)
    uint8_t    heap1[MAX_HEAP] __attribute__((aligned(4)));  // shadow (DOUT)
    // allow device output latch=0 or disallow latch=1
    uint8_t latch;
    // check if any node has been set: anyx|anyd == CSP_TRUE
    int8_t  anyd;  // CSP_TRUE|CSP_FALSE
    bitset_decl(dset, MAX_INDEX); // mark decl updated during cycle
    
    index_t offs[MAX_OBJECTS];     // offset to object locals
    // stack used during eval
    int esp;                       // eval stack pointer
    struct PACKED { index_t ix; unsigned cur:OBJ_BITS; }
	stack[MAX_STACK_DEPTH];
    unsigned reactive:1;         // 1 if push backedges to queue
    unsigned sweep:1;            // 1 during a full sequential sweep (csp_eval /
				 // reactive seed): OP_NEW/LEAVE enter/leave objects.
				 // 0 during csp_react single-rule dispatch.
    unsigned seed_all:1;         // 1 during the first cycle: OP_CHG reads true for
				 // every input so each <- binding fires once to
				 // establish its initial value (least surprise).

    // Firmware ROM executes in place from flash (see doc/ROM_RAM.md); RAM holds
    // patches. The logical index space is [0,rom_n*) = ROM (read via the pointers
    // below), [rom_n*, .) = RAM (ram_*[logical - rom_n*]). rom_n*==0 => no ROM
    // active, everything is RAM. Also the ROM/RAM boundary /list tags against.
    const csp_decl_t*  rom_decl_p;  // ROM decl table (flash), or NULL
    const csp_instr_t* rom_instr_p; // ROM instr table (flash), or NULL
    const char*        rom_str_p;   // ROM string table (flash), or NULL
    index_t rom_nd;              // # ROM decls   (RAM decl base)
    index_t rom_nn;              // # ROM instrs  (RAM instr base)
    index_t rom_strp;            // # ROM string bytes (RAM string base)
    index_t rom_ns;              // # baseline states (INIT/NORMAL + ROM states);
				 // EEPROM persists only the runtime additions above

    csp_pstate_t ps;             // parse state
    reg_allocator_t* ap;
    int ev;                      // eval variables when ev=1
    int sdef;                    // current state (compile time)
    index_t in_marker;           // instr index of the pending OP_INSTATE block
				 // gate (patched with the skip distance at #end)
    int list_state;              // during listing: state of the #in block being
				 // rendered (-1 = none), suppresses State==S in cond
    index_t save_sx;             // save sx during module parse
    index_t sx;                  // runtime state, state variable    
    state_t states[MAX_STATES];  // declared states
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
    // temp var list during <- parsing (reuses timer[], set by csp_rt_init)
    index_t* var;
    index_t nvar;
    int     rimp;                // 1 if parse_expr is in RHS in <- 
    // during eval
    uint32_t update;             // update counter
    uint32_t wait_ms;            // sleep time or NOTIMEOUT
#if defined(SUPPORT_REACTIVE) && (SUPPORT_REACTIVE==1)        
    bitset_decl(inq, MAX_QENTRY); // mark queued (obj,ip) entries during eval
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
    uint8_t ufuncs_rom;          // 1 if the ufuncs table is in ROM (PROGMEM)
    // user hook to lookup platform constants
    csp_const_fn uconst;
} csp_rt_t;

// Read a whole RO record by value. On AVR the ROM segment is in PROGMEM, so we
// copy it into a RAM temporary -- then bit-field access works as usual. (The
// "clever" bit: never deref a PROGMEM struct directly.)
#if defined(__AVR__)
static inline csp_decl_t  ro_decl(const csp_decl_t* p)
{ csp_decl_t d;  memcpy_P(&d, p, sizeof(d)); return d; }
static inline csp_instr_t ro_instr(const csp_instr_t* p)
{ csp_instr_t v; memcpy_P(&v, p, sizeof(v)); return v; }
static inline state_t     ro_state(const state_t* p)
{ state_t s; memcpy_P(&s, p, sizeof(s)); return s; }
#else
#define ro_decl(p)  (*(p))
#define ro_instr(p) (*(p))
#define ro_state(p) (*(p))
#endif

// Segment-aware read by logical index: a firmware ROM index reads flash (via the
// PROGMEM-safe ro_decl/ro_instr), a RAM index reads ram_*[logical - base]. ROM
// is never written -- writes go to the RAM slots (ram_decl_at/ram_instr_at),
// whose logical index is always >= the base. With no ROM active (rom_n*==0) all
// of these reduce to plain ram_* access. NOINLINE (defined in csp_rt.c) so the
// flash-copy is not expanded at every decl()/instr() site (code size on AVR).
extern csp_decl_t  csp_get_decl(csp_rt_t* st, index_t i);
extern csp_instr_t csp_get_instr(csp_rt_t* st, index_t n);

// one string byte at a logical position (length byte or char)
static inline uint8_t csp_str_byte(csp_rt_t* st, sindex_t pos)
{
    if (pos < (sindex_t)st->rom_strp)
	return ro_byte(&st->rom_str_p[pos]);
    return (uint8_t)st->ram_str[pos - st->rom_strp];
}

// char* to the string at a logical position (host: RODATA is normal memory;
// an AVR PROGMEM string needs a copy-out API -- deferred). Base 0 -> ram_str.
static inline char* csp_str_at(csp_rt_t* st, sindex_t pos)
{
    if (pos < (sindex_t)st->rom_strp)
	return (char*)&st->rom_str_p[pos];
    return &st->ram_str[pos - st->rom_strp];
}

// RAM write slots -- logical index must be at/above the ROM base (RAM region)
#define ram_decl_at(st, logical)  (&(st)->ram_decl[(logical) - (st)->rom_nd])
#define ram_instr_at(st, logical) (&(st)->ram_instr[(logical) - (st)->rom_nn])
#define ram_str_at(st, logical)   ((st)->ram_str[(logical) - (st)->rom_strp])

#define decl(st,i,fld)  (csp_get_decl((st),(i)).fld)
#define instr(st,n,fld) (csp_get_instr((st),(n)).fld)

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
	    unsigned part:PART_BITS; // csp_part_t, PART_VAL for the plain value
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

// Resolve a leaf index to its view descriptor (see doc/DESCRIPTORS.md).
// Step 2: table-driven. Every entry is still VIEW_SLOT with slot == st_index,
// so behaviour is identical to before. Step 3 starts emitting VIEW_HEAP.
static inline csp_view_t* csp_view(csp_rt_t* st, index_t n)
{
    return &st->view[st_index(st, n)];
}

#if defined(SUPPORT_REACTIVE) && (SUPPORT_REACTIVE==1)
// enq a rule for recalculation, with object context
static inline void csp_enq(csp_rt_t* st, uint8_t obj, uint16_t ip)
{
    index_t e = MAKE_QENTRY(obj, ip);   // dedup per (obj,ip): the same rule runs
    if (bitset_tst(st->inq, e))         // once per object, not once overall
	return;
    if ((st->tl - st->hd) != MAX_QUEUE) {
	st->queue[st->tl % MAX_QUEUE] = e;
	st->tl++;
	bitset_set(st->inq, e);
    }
}

// deq returns packed (obj, ip) - use QENTRY_OBJ/QENTRY_IP to unpack
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

extern value_t* csp_dio_slot(csp_rt_t* st, index_t ix, dio_t dir);
extern int csp_dio_slots(csp_rt_t* st,index_t ix,value_t** iptr,value_t** optr);
extern void csp_dio_set(csp_rt_t* st, index_t ix, value_t v, dio_t dir);
extern void csp_dio_get(csp_rt_t* st, index_t ix, value_t* vp, dio_t dir);

extern value_t  csp_value(csp_rt_t* st, index_t x);

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
    // name is a LOGICAL string position: ROM range -> flash table, else RAM.
    // (On the host RODATA is ordinary memory; an AVR PROGMEM name needs a
    // copy-out API -- deferred.) At base 0 this is plain ram_str access.
    return csp_str_at(st, csp_get_decl(st, INDEX(ix)).name);
}

// logical string position of a decl's name (for the segment-aware str helpers)
static inline sindex_t decl_name_pos(csp_rt_t* st, index_t ix)
{
    return csp_get_decl(st, INDEX(ix)).name;
}

extern int     csp_rt_init(csp_rt_t*,  int reactive);
extern void    csp_load_rom(csp_rt_t*);
extern int     csp_has_firmware(void);
extern int     csp_rt_start(csp_rt_t*);
extern void    csp_set_ufuncs(csp_rt_t*, const csp_func_t*, uint8_t count, uint8_t rom);
extern void    csp_set_uconst(csp_rt_t*, csp_const_fn uconst);
extern const csp_func_t* csp_match_func(csp_rt_t*,
					const tstr_t* name,
					uint8_t arity, rentry_t* rarg,
					int* is_user, int* func_idx);
extern int     csp_set_reactive(csp_rt_t*, int onoff);
extern int     csp_set_latch(csp_rt_t*, int onoff);
extern int     csp_scan_line(csp_rt_t*,char* str,token_t* tv,size_t* num_toks);
extern int     csp_parse(csp_rt_t*, char* str);
extern void    csp_csr(csp_rt_t* st);
// Segment-aware string helpers: operate on a logical string position (ROM in
// flash or RAM), so they are AVR-PROGMEM-safe where csp_str_at's raw pointer is
// not. NOINLINE to keep the flash-access logic in one place (code size).
extern int  csp_str_ncmp(csp_rt_t* st, sindex_t pos, const char* s, int n);
extern int  csp_str_eq(csp_rt_t* st, sindex_t pos, const char* s, int n);
extern void csp_print_str_at(csp_rt_t* st, sindex_t pos);
extern index_t csp_eval(csp_rt_t* st);
extern index_t csp_eval_range(csp_rt_t* st, index_t start, index_t stop);
extern int     csp_eval_rule(csp_rt_t* st, int);
extern index_t csp_react(csp_rt_t* st);
extern index_t csp_cycle(csp_rt_t* st);   // one cycle: mixes ROM/RAM modes
extern void    csp_undo(csp_rt_t* st);
extern void    csp_commit(csp_rt_t* st);

extern void csp_set_value(csp_rt_t* st, index_t n, value_t v);
extern void csp_set_ivalue(csp_rt_t* st, index_t n, ivalue_t v);
extern void csp_set_fvalue(csp_rt_t* st, index_t n, fvalue_t v);
extern void csp_set_dvalue(csp_rt_t* st, index_t n, uvalue_t u);
extern void csp_set_avalue(csp_rt_t* st, index_t n, uvalue_t u);
extern void csp_set_tvalue(csp_rt_t* st, index_t n, uvalue_t u);

extern int csp_parse_expr(csp_rt_t* st, const token_t* tv, size_t* num_toks,
			  rentry_t* result);
extern int csp_parse_const_expr(csp_rt_t* st, const token_t* tv, size_t* num_toks,
				rentry_t* result);
//
extern index_t csp_new_decl(csp_rt_t* st, const tstr_t* name, decl_t op);
extern index_t csp_lookup_decl(csp_rt_t* st, const tstr_t* name);

// backend port (linux/arduino/LPCopen/FreeRTOS)
extern uint32_t csp_time_ms(void);
extern unsigned long csp_time_us(void);
extern void csp_setup(csp_rt_t* st);
extern void csp_input(csp_rt_t* st);
extern void csp_output(csp_rt_t* st);
// common timer processing 
extern void csp_input_timer(csp_rt_t* st);
extern void csp_output_timer(csp_rt_t* st);

// eeprom save/load (csp_eeprom.c)
extern int csp_eeprom_save(csp_rt_t* st);
extern int csp_eeprom_load(csp_rt_t* st);
extern int csp_eeprom_size(csp_rt_t* st);
extern int csp_eeprom_clear(csp_rt_t* st);

// stack check/debug
extern int stack_used();

extern const char  csp_tag(csp_rt_t* st, index_t n);
extern rochar* csp_fmt_pindir(uint8_t dir);
extern rochar* csp_fmt_pull(csp_rt_t* st, int ix);
extern rochar* csp_fmt_pwm(csp_rt_t* st, int ix);
extern rochar* csp_fmt_vtype(vtype_t vt);
extern rochar* csp_fmt_endian(vendian_t et);
extern rochar* csp_format_error(csp_err_t err);

extern const char* csp_opcode_name(opcode_t op);
extern uint8_t csp_opcode_rtype(opcode_t op);
extern uint8_t csp_opcode_arity(opcode_t op);

extern int csp_opcode_to_tok(opcode_t opcode);
extern uint8_t csp_opcode_rtype(opcode_t opcode);
// csp_set_error return 1 if error was set and arguments can be defined!
extern int csp_set_error(csp_rt_t*, csp_err_t);
extern void csp_set_err_arg_tstr(csp_rt_t*, int i, const tstr_t* str);
extern void csp_set_err_arg_ix(csp_rt_t*, int i, index_t ix);
extern void csp_clr_error(csp_rt_t*);
#if defined(SUPPORT_REACTIVE) && (SUPPORT_REACTIVE==1)
extern void csp_enq_elist(csp_rt_t* st, index_t x);
#endif

// Interactive command handling
#define CSP_CMD_OK       0
#define CSP_CMD_QUIT     1
#define CSP_CMD_NOTFOUND -1
#define CSP_CMD_ERROR    -2

typedef int (*csp_cmd_fn)(csp_rt_t* st, int argc, char* argv[]);

typedef struct {
    rochar* name;
    rochar* help;
    csp_cmd_fn fn;
} csp_cmd_t;

extern int csp_cmd_dispatch(csp_rt_t* st, char* cmd);
extern void csp_cmd_help(void);
extern int csp_process_line(csp_rt_t* st, char* line);

// Line input handling (shared between platforms)
#if defined(__AVR__)
#define CSP_LINE_BUF_SIZE 64
#else
#define CSP_LINE_BUF_SIZE 128
#endif
extern char csp_line_buf[CSP_LINE_BUF_SIZE];
extern uint8_t csp_line_pos;
extern uint8_t csp_line_ready;

extern void csp_line_init(void);
extern void csp_line_input(char c);
extern void csp_line_prompt(void);

// Platform hooks for commands (implemented per platform)
extern int csp_cmd_save(csp_rt_t* st, int argc, char* argv[]);
extern int csp_cmd_load(csp_rt_t* st, int argc, char* argv[]);

// eeprom api
extern int csp_eeprom_open_read(void);
extern int csp_eeprom_open_write(void);
extern void csp_eeprom_close(void);
extern int csp_eeprom_read(void* buf, size_t len);
extern int csp_eeprom_write(const void* buf, size_t len);

#ifdef __cplusplus
EXTERN_C_END
#endif

#endif
