// micro-csp: a token-threaded machine for the COLD half of the runtime.
//
// Most of a CandySpeak image is code that waits for a human to type a line:
// on mega_bare csp_compile, csp_repl, csp_print and csp_parse are 57% of the
// flash between them. Cycle count there is free, and AVR's encoding is not --
// two-byte instructions, three pointer registers and no scaled indexing mean
// gcc spends most of a cold function building addresses. A one-byte token
// stream is three to four times denser, and the interpreter is paid once.
//
// TWO IMPLEMENTATIONS, ONE SEMANTICS. This file is the portable C one: it is
// the reference, it is what other ports link, and it is what `make test` runs
// the bytecode through on a workstation. An AVR assembly dispatch (six
// instructions, ~9 cycles) is a drop-in for it and is validated by running the
// same bytecode. Nothing about the token stream is target-specific.
//
// CELLS ARE SIXTEEN BITS ON EVERY TARGET, host included. Anything else and the
// host tests pass while AVR truncates -- the exact shape of bug that
// utils/width_check.sh exists to catch. It also means a cell CANNOT hold a host
// pointer, so there is no fetch-from-raw-address in the vocabulary. That is not
// a limitation: the words here take a declaration INDEX, an instruction INDEX
// and a field id, never an address. Domain words are what make 16-bit cells
// enough, which is the argument for them over a general stack machine.
#ifndef __CSP_MCSP_H__
#define __CSP_MCSP_H__

#include <stdint.h>

typedef uint16_t mc_cell_t;

// Opcodes. The order is the jump-table order, so the AVR port's `.balign`
// table follows it entry for entry; adding one in the middle renumbers the
// bytecode and gen/ has to be regenerated. Append.
typedef enum {
    MC_BYE    =  0,   // stop; TOS is the result
    MC_LIT8   =  1,   // <b>      push b, zero-extended
    MC_LIT16  =  2,   // <lo><hi> push a full cell
    MC_DROP   =  3,
    MC_DUP    =  4,
    MC_SWAP   =  5,
    MC_OVER   =  6,
    MC_ADD    =  7,
    MC_SUB    =  8,
    MC_AND    =  9,
    MC_OR     = 10,
    MC_INC    = 11,
    MC_DEC    = 12,
    MC_EQ     = 13,   // TOS = (NOS == TOS); 1 or 0
    MC_NE     = 14,
    MC_LT     = 15,   // UNSIGNED, like the cell
    MC_ZEQ    = 16,   // TOS = (TOS == 0)
    MC_JMP    = 17,   // <rel8>   signed, relative to the byte after it
    MC_JZ     = 18,   // <rel8>   pop; branch if it was zero
    MC_CALL   = 19,   // <lo><hi><frame> call a word in the same stream
    MC_EXIT   = 20,
    MC_TOR    = 21,   // data -> return stack
    MC_RFROM  = 22,   // return -> data stack
    MC_RAT    = 23,   // copy top of return stack
    MC_DECL   = 24,   // <fld>    TOS = field fld of declaration TOS
    MC_INSTR  = 25,   // <fld>    TOS = field fld of instruction TOS
    MC_ND     = 26,   // push the declaration count
    MC_NN     = 27,   // push the instruction count
    MC_NATIVE = 28,   // <n>      call leaf n: TOS in, TOS out
    MC_NATIVEN= 29,   // <n>      call stack-leaf n: it takes what it wants
    MC_LGET   = 30,   // <k>      push local k
    MC_LSET   = 31,   // <k>      pop into local k
    MC_NOPCODE
} mc_op_t;

// TWO LEAF FORMS, because one arity does not fit both cases.
//
// MC_NATIVE is one cell in, one cell out. r24:r25 is where avr-gcc passes a
// single 16-bit argument AND where it returns a 16-bit result, so on the
// target this costs not one instruction of shuffling -- and most of what the
// listing path calls takes exactly one argument. (The IP lives in r4:r5, not
// X, because r2-r17 are call-saved under that ABI and X is not, which is what
// makes an ordinary C function a legal leaf with no glue at all.)
typedef mc_cell_t (*mc_leaf_t)(void* ctx, mc_cell_t tos);

// MC_NATIVEN hands the leaf the STACK POINTER and takes back the new one. The
// leaf reads sp[0] as the top, sp[1] as the next, and returns where the stack
// should be left -- so one register carries both "how many I consumed" and
// "what I put back", and a leaf may push more than one cell. An out-parameter
// pair (npop, value) says the same thing in two memory writes and cannot
// return two cells.
//
// THE STACK GROWS DOWN so this view is the same on both machines: sp[0] is the
// top on AVR, where Y is the stack pointer, and the reference below does the
// same. A leaf is ordinary C compiled for both -- it must not see two layouts.
typedef mc_cell_t* (*mc_leafn_t)(void* ctx, mc_cell_t* sp);

// A bit-field of a packed record, as (word, shift, width). The decl records are
// two 32-bit words of bit-fields, so a field is read by loading its word,
// shifting it down and masking -- which is what gcc emits inline today, once
// per call site. Here it is one table row and one token.
//
// THE TABLE CANNOT BE DERIVED: offsetof does not apply to a bit-field, so these
// rows are written by hand and are wrong the moment a width in csp.h changes.
// tests/mcsp.c writes each field through the C struct and reads it back through
// its row; that round trip is the only thing keeping them honest.
typedef struct {
    uint8_t word;    // which 32-bit word of the record
    uint8_t shift;   // bit position within it
    uint8_t bits;    // width, 1..16 (a cell is 16 bits -- wider fields need
                     // their own word, not a truncation nobody sees)
} mc_field_t;

// Field ids for MC_DECL are GENERATED: gen/csp_layout.h carries the MF_* enum
// and CSP_DECL_COMMON_FIELDS, both computed from utils/layout.terms. They were
// written out by hand here once, which meant two descriptions of one bit layout
// -- the exact hazard the terms file removes. The array is declared without a
// size so this header does not have to pull the generated one in.
extern const mc_field_t mc_decl_fields[];
extern const uint8_t    mc_decl_nfield;

// Read one field out of a record already in RAM. Pure, and deliberately so:
// this is the part that can be wrong in a way nothing else notices, and a pure
// function is one a test can hammer without a runtime around it.
extern mc_cell_t mc_field_get(const void* rec, const mc_field_t* f);

// Everything the machine needs. The caller owns the stacks, so a port decides
// what it can afford -- and on a board where the pool and the stack are the
// same RAM (see doc/AVR_CODE_SIZE.md) that is not a detail.
typedef struct {
    const uint8_t* code;      // token stream (RODATA/flash)
    uint16_t       code_len;
    mc_cell_t*     ds;        // data stack base; grows DOWN from ds[ds_size]
    uint8_t        ds_size;
    uint16_t*      rs;        // return stack, grows UP
    uint8_t        rs_size;
    // Locals. Generated code does NOT juggle the stack to hold a variable --
    // hand-encoding setup_routes needed five tokens of TOR/RAT/RFROM for what C
    // writes as n++, and a code generator should never emit that.
    //
    // FRAMED: MC_CALL carries the CALLER's frame size and pushes the old base
    // on the return stack, so a called word's locals sit above its caller's
    // instead of on top of them. The size is at the call site because that is
    // the only place that knows it -- the callee's entry is just an offset.
    mc_cell_t*     lv;
    uint8_t        lv_size;
    const mc_leaf_t*  leaf;   // one-in one-out natives  (MC_NATIVE)
    uint8_t           nleaf;
    const mc_leafn_t* leafn;  // stack-form natives       (MC_NATIVEN)
    uint8_t           nleafn;
    void*          ctx;       // handed to every leaf; the runtime, normally
    // Arguments for the entry word, pushed left to right before it starts --
    // the same place a word's caller leaves them, so the entry word is not a
    // special case with its own convention.
    const mc_cell_t* arg;
    uint8_t          nargs;
} mc_vm_t;

// Result codes. A bad token or a stack that ran off its end is a BUG in the
// generator, not an input error, so it stops rather than limping.
#define MC_OK          0
#define MC_E_OPCODE   -1
#define MC_E_STACK    -2
#define MC_E_BOUNDS   -3
#define MC_E_LEAF     -4

// How the domain words reach the runtime. Set once by whoever wires micro-csp
// to a csp_rt_t; left NULL, MC_DECL and MC_ND return MC_E_LEAF instead of
// dereferencing nothing. Indirect so a test can drive the machine without the
// runtime linked behind it.
extern const void* (*mc_decl_hook)(void* ctx, mc_cell_t i);
extern mc_cell_t   (*mc_nd_hook)(void* ctx);

extern int csp_mcsp_run(mc_vm_t* vm, uint16_t entry, mc_cell_t* result);

#endif
