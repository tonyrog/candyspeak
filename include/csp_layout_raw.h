// The bit-field structs the record layout USED to be, and the unions they went
// into.  Nothing in the runtime may include this.
//
// utils/layout.terms is the layout now, and gen/csp_layout.h the only way to a
// field -- csp_decl_t and csp_instr_t are opaque byte arrays so that naming a
// bit-field does not even compile.  What is left here is for the LAYOUT TESTS,
// which have to hold both descriptions at once in order to compare them:
// tests/layout.c field by field, tests/states_layout.c for the aliasing a
// states block depends on, tests/instr_layout.c for SETO against SETOX.
//
// When those checks are replaced by something that does not need a second
// description, this file goes with them.
#ifndef __CSP_LAYOUT_RAW_H__
#define __CSP_LAYOUT_RAW_H__

#define INSTR_COMMON \
    opcode_t op:CSP_OPCODE_BITS

// u: the operands are UNSIGNED. Only seven opcodes care -- / % >> < <= > >= --
// and everything else (+ - * & | ^ == !=) gives the same bits either way.
//
// A FLAG and not seven more opcodes: OP_AVAIL is already 60 of the 63 the 6-bit
// field can hold, so an unsigned mirror of each would not fit. The word has room
// -- op(6) + three registers(4) is 18 of 32 -- and an image compiled before this
// existed reads back with u == 0, which is the signed behaviour it had.
//
// swap: the operands were EXCHANGED to get here. `a > b` is emitted as `b < a`,
// which is why there is no OP_GT: the runtime already computes the answer, and
// four opcodes buy nothing a swap of two register numbers does not.
//
// Nothing reads it at RUN time -- it is a note for the LISTING. To render the
// source back, both halves have to be undone: exchange the operands AND mirror
// the operator (`LT y=b z=a` -> `a > b`). Doing only one gives `a < b` or
// `b > a`, which are different programs. See exprbuf_expr.
//
// Only the ordered comparisons ever set it. `==` and `!=` are symmetric, so a
// swap on them would be a bit that never means anything.
typedef struct PACKED {
    INSTR_COMMON;
    unsigned x:REG_BITS;
    unsigned y:REG_BITS;
    unsigned z:REG_BITS;
    unsigned u:1;
    unsigned swap:1;    // y <-> z: `y < z` was written `z > y`
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
    unsigned mem:INDEX_BITS;  // declaration: variable/constant
} csp_instr_mem_t;
typedef struct PACKED {
    INSTR_COMMON;
    unsigned x:REG_BITS;      // destination register
    signed imm:TINY_BITS;     // signed tiny immediate bits
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
    signed   nxt:15;       // relative jump if !cnd (was int16 -- 15 bits is plenty)
    unsigned implicit:1;   // 1 = bare NORMAL+ rule: list bare, suppress its
			   // implicit State==INIT||State==NORMAL guard
} csp_instr_rule_t;

typedef struct PACKED {
    INSTR_COMMON;
    unsigned x:REG_BITS;   // body result
} csp_instr_next_t;

// op INSTATE - #in <state> block gate. A LD of the state variable precedes it;
// if that register != imm, jump nxt to skip the whole block (sequential path).
// OP_NINSTATE shares this layout but inverts the test: if x == imm, jump nxt to
// enter the block (used to OR-chain a multi-state `#in A B C`, see csp_parse_in).
// implicit: set on the auto NORMAL+ gate wrapping a bare top-level rule, so the
// listing renders that rule bare instead of emitting a `#in NORMAL` header.
typedef struct PACKED {
    INSTR_COMMON;
    unsigned x:REG_BITS;   // register holding the current State value
    signed   imm:8;        // target state number
    signed   nxt:13;       // relative jump (skip block if !=, enter block if ==)
    unsigned implicit:1;   // 1 = auto NORMAL+ wrap: list the rule bare
} csp_instr_instate_t;

typedef struct PACKED {
    INSTR_COMMON;
    unsigned num:BODY_BITS;   // number of instructions (shares the word with mx)
    index_t  mx;     // module index
} csp_instr_enter_t;

// A string segment header. `num` payload slots follow it, holding identifier
// text; `used` is how many BYTES of them are in use, which is what lets an
// image say how much string space it carries without a header field (only the
// last segment's value is ever read).
typedef struct PACKED {
    INSTR_COMMON;
    unsigned num:BODY_BITS;   // payload slots that follow
    unsigned used:8;          // bytes used in this segment
} csp_instr_seg_t;

typedef struct PACKED {
    INSTR_COMMON;
    unsigned num:BODY_BITS;   // number of instructions (shares the word with mx)
    index_t  mx;     // module index
} csp_instr_leave_t;

// Instantiate an object: enter its module body like a call.
//
// The entry point is NOT here. It used to be `ent:BODY_BITS`, an ABSOLUTE
// instruction index in ten bits -- so a module whose ENTER landed past 1023 had
// its entry truncated silently: 1557 became 533, and the object called into the
// middle of unrelated code with no error anywhere. The module DECLARATION
// already carries it as a full index_t, reachable from obj in two steps
// (obj_entry), which costs nothing at a call and removes the ceiling.
typedef struct PACKED {
    INSTR_COMMON;
    index_t  obj;            // object declaration index
} csp_instr_new_t;

typedef struct PACKED {
    INSTR_COMMON;
    unsigned x:REG_BITS;     // result register
    unsigned idx:FUNC_BITS;  // function index
    unsigned usr:1;          // user function
    unsigned avt:16;         // argument value types 4 bit per argument
} csp_instr_call_t;

// OP_SETO - point CURRENT at a NAMED object (`safe.State`), for ONE access.
//
// An encoded index has a single selector bit -- global or current object -- so a
// reference to a named object cannot say which one. This instruction says it, and
// the memory instruction that FOLLOWS reads CURRENT-relative.
//
// One-shot: the next memory op consumes it and the runtime puts the object
// context back (see eval_op). That is what makes it safe to place anywhere. A
// sticky base register would have to be paired with a restore, and a rule's
// conditional jump (csp_instr_rule_t.nxt) can skip forward over instructions --
// so a taken jump could leave the base pointing at the wrong object for whatever
// ran next. There is nothing to leave stale here.
typedef struct PACKED {
    INSTR_COMMON;
    unsigned obj:16;         // object table index (1..MAX_OBJECT_NUM)
} csp_instr_seto_t;

// OP_SETOX - the same one-shot as OP_SETO, but for an ARRAY ELEMENT chosen at
// runtime: `P[Idx]`.
//
// NOT an object number. OP_SETO names an object and looks up offs[]; this one
// shifts the base by an ELEMENT, which is why an array costs no DECL_OBJECT, no
// offs[] slot and no object[] slot per element:
//
//     cbase = offs[cur] + reg * stride
//
// and the memory instruction that follows adds the array's own index, so it
// lands on element `reg`. offs[cur] keeps it correct inside a module, where the
// array's index is module-relative; at global level offs[0] is 0 and the base
// is just reg*stride.
//
// An array occupies `len` CONSECUTIVE declarations, one per element -- view[] is
// indexed by declaration index (see st_index), so elements cannot share one
// declaration without colliding with whatever is declared next. That is a cost
// (8 bytes per element) and a feature: each element carries its OWN config, so
// `#analog P[10] out 9:0..9` gives every element its own pin with no special
// case in setup.
//
// stride is members per element: 1 for a scalar array, the module's member count
// for an array of instances. 6 bits, so an arrayed module tops out at 63
// members.
//
// UNLIKE OP_SETO the operand is not trusted -- a register holds whatever the
// program computed. `len` is here so the check is the RIGHT one: `P[99]` fails
// against the array's own length rather than merely staying inside the arena.
typedef struct PACKED {
    INSTR_COMMON;
    unsigned x:REG_BITS;     // register holding the element index
    unsigned len:16;         // element count, for the bounds check
    unsigned stride:6;       // declarations per element (1 = scalar array)
} csp_instr_setox_t;

// OP_END_MARK: a self-verifying terminator appended after rom_instr's data. Its
// crc is a CRC-16 over [the section's data + this marker with crc zeroed], so the
// instruction section can be verified WITHOUT the header -- scan for OP_END_MARK,
// its position is the section length, its crc confirms integrity. _res pads so
// crc lands byte-aligned at bytes 2-3 (INSTR_COMMON is op:6). See rom_scan_end.
typedef struct PACKED {
    INSTR_COMMON;            // op == OP_END_MARK (0x3f)
    unsigned _res:10;        // pad to a 2-byte boundary
    uint16_t crc;            // section self-CRC (bytes 2-3)
} csp_instr_end_t;
typedef union {
    struct PACKED { INSTR_COMMON; uint32_t rest:26; };
    csp_instr_enter_t e;
    csp_instr_seg_t   sg;
    struct PACKED { uint8_t b[4]; } raw;
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
    csp_instr_seto_t o;
    csp_instr_setox_t ox;
    csp_instr_end_t em;
} csp_instr_raw_t;
#define DECL_HEADER \
    decl_t type:CSP_DECL_TYPE_BITS; \
    unsigned cont:1; \
    unsigned local:1; \
    pindir_t dir:DIR_BITS; \
    unsigned name:NAMEID_BITS
#define DECL_TYPE_HEADER \
    DECL_HEADER; \
    unsigned res:5; \
    unsigned is_mapped:1; \
    unsigned bound:1; \
    unsigned nx:1;    /* RESERVED: name's 9th bit; see utils/layout.terms */ \
    unsigned vt:TYPE_BITS; \
    unsigned reg:REG_BITS
#define DECL_COMMON DECL_TYPE_HEADER

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
    unsigned irq:3;      // trigger_t; IRQ_NONE = not an interrupt source
    unsigned soft:1;     // sampling is acceptable -- see decl_opts_t.soft
} csp_digital_t;
typedef struct PACKED {
    DECL_COMMON;
    index_t n;          // number of nodes in module definition
    index_t ent;        // entry point in instr
} csp_module_t;

typedef struct PACKED {
    DECL_COMMON;    
    index_t  mx;           // module declaration index
    unsigned m:16;         // index in object table (1..MAX_OBJECT_NUM)
} csp_object_t;

// Up to 6 state names per declaration: DECL_HEADER (17) + 5 x 9 = 62 of 64 bits.
//
// DECL_HEADER, not a hand-copied prefix: `name` is slot 0, so a states block IS
// a declaration with a name -- the one every other reader already knows how to
// look at -- and the alias cannot drift, because there is only one definition of
// it now. What DECL_TYPE_HEADER adds on top (vt, res, is_mapped, bound, reg)
// lands on name2 and name3; see tests/states_layout.c.
typedef struct PACKED {
    DECL_HEADER;                  // type, dir, and slot 0 as `name`
    unsigned name2:NAMEID_BITS;
    unsigned name3:NAMEID_BITS;
    unsigned name4:NAMEID_BITS;
    unsigned name5:NAMEID_BITS;
    unsigned name6:NAMEID_BITS;
    // RESERVED: byte 7, one extension bit per slot with two to spare. The whole
    // byte rather than six bits -- it zeroes in one store and reads as one
    // number in a hex dump of an image.
    unsigned nx:8;
} csp_states_t;

typedef struct PACKED {
    DECL_COMMON;
    unsigned pin:PIN_BITS;
    unsigned port:PORT_BITS;
    unsigned pwm:1;    // pwm output
    unsigned endian:2; // |little|big
    unsigned irq:3;    // trigger_t; IRQ_NONE = not an interrupt source
    unsigned soft:1;   // sampling is acceptable -- see decl_opts_t.soft
} csp_analog_t;

typedef struct PACKED {
    DECL_COMMON;
    unsigned id:INDEX_BITS; // the #buffer this field is a view into
    unsigned endian:2; // |little|big
    unsigned bit:9;   // 0-511   // bit start pos
    unsigned len:5;   // (1-32)  // data length -1
} csp_field_t;

// #buffer. Its size does NOT live in DECL_COMMON.res: that is 5 bits holding
// bits-1, so anything past 32 bits truncated silently (a 64-bit buffer became
// 4 bytes). nbytes here is the one source of truth for how big a buffer is.
typedef struct PACKED {
    DECL_COMMON;
    unsigned nbytes:10;     // 1..1023; a CAN FD frame is 64 bytes
    // 4 bits, not 2: TR_UDP is 5. The word had four spare bits, so this costs
    // nothing -- csp_bufdecl_t is 8 bytes before and after.
    unsigned transport:4;   // transport_t: TR_NONE plain RAM, TR_CAN a frame
    unsigned id:INDEX_BITS; // the constant holding this transport's endpoint:
			    // a frame id, a packed bus/addr/reg, or an IPv4
			    // address. See transport_t for the packings.
			    //
			    // TR_UDP USES TWO: `id` holds the IPv4 address and
			    // `id + 1` the port. A constant is 32 bits and this
			    // endpoint is 48, and a port field here would take
			    // csp_decl_t from 8 bytes to 12 -- four bytes on
			    // every declaration of every kind, to carry one
			    // number for one transport. csp_parse_buffer makes
			    // the pair with new_signed_const twice and REFUSES
			    // to compile if they did not come out adjacent, so
			    // the assumption cannot rot quietly.
} csp_bufdecl_t;

// #route. Two buffer DECLARATIONS -- resolved to buffer ids at setup, so the
// pairing survives a rebuild that renumbers nothing and a ROM image that
// carries the declarations and not the tables.
typedef struct PACKED {
    DECL_COMMON;
    unsigned src:INDEX_BITS;   // where the bytes come from
    unsigned dst:INDEX_BITS;   // and where they go
} csp_route_t;

typedef struct PACKED {
    DECL_COMMON;
    unsigned long period:28; // timeout value ms (74h max)
    unsigned _res:1;         // reserved
    unsigned fired:1;        // timeout occurred this cycle (edge-triggered)
    unsigned running:1;      // timer is runnig (tx is valid time)
    unsigned init:1;         // one bit value 1 = start, 0 = stop
} csp_timer_t;

// DECL_END_MARK: self-verifying terminator appended after rom_decl's data (the
// counterpart to csp_instr_end_t for the decl section). DECL_COMMON is exactly 4
// bytes, so crc lands byte-aligned at bytes 4-5. See rom_scan_end.
typedef struct PACKED {
    DECL_COMMON;             // type == DECL_END_MARK (0xf)
    uint16_t crc;            // section self-CRC (bytes 4-5)
    uint16_t _res;           // pad to 8 bytes
} csp_decl_end_t;
typedef union {
    struct PACKED { DECL_COMMON; };
    csp_module_t   md;
    csp_object_t   mq;
    csp_variable_t va;
    csp_constant_t cn;
    csp_digital_t  di;
    csp_analog_t   an;
    csp_field_t    ca;
    csp_bufdecl_t  bf;
    csp_route_t    rt;
    csp_timer_t    tm;
    csp_states_t   s6;
    csp_decl_end_t em;
} csp_decl_raw_t;

#endif
