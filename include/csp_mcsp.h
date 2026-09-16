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
    MC_NATIVE = 28,   // <n>      call leaf n: TOS in, TOS out
    MC_NATIVEN= 29,   // <n>      call stack-leaf n: it takes what it wants
    MC_LGET   = 30,   // <k>      push local k
    MC_LSET   = 31,   // <k>      pop into local k
    MC_BUF    = 32,   // <fld>    TOS = field fld of buf TOS
    MC_ST     = 33,   // <fld>    TOS = field slf of state TOS
    MC_VIEW   = 34,   // <fld>    TOS = field fld of view TOS    
    // ---- DOUBLES -------------------------------------------------------
    //
    // A double is TWO CELLS with the MOST SIGNIFICANT on top, which is what
    // Forth does and what makes the carry algorithms read the way the textbook
    // writes them. sp[0] is the high half, sp[1] the low.
    //
    // Separate opcodes rather than letting MC_DECL push one cell or two
    // depending on the field: the stack effect has to be readable from the
    // bytecode, not from whatever utils/layout.terms says today.
    MC_DECL2  = 35,   // <fld>    TOS = declaration index -> wide field
    MC_INSTR2 = 36,   // <fld>
    MC_BUF2   = 37,   // <fld>
    MC_VIEW2  = 38,   // <fld>
    MC_DLIT   = 39,   // <b0><b1><b2><b3>  push a 32-bit literal
    MC_S2D    = 40,   // widen the top cell to a double (zero-extended)
    MC_DDROP  = 41,
    MC_DADD   = 42,   // ( dl dh el eh -- sl sh )
    MC_DSUB   = 43,
    MC_DEQ    = 44,   // two doubles -> ONE cell, 1 or 0
    MC_DLT    = 45,   // unsigned, like the cell

    // WRITES. `( v i -- )` and `( lo hi i -- )` for a double: the value goes
    // down first and the index on top, which is Forth's order for `!` and the
    // order a generated expression falls out in anyway.
    //
    // A SECOND set of hooks, not the read ones. A read may come from the decl
    // cache -- a RAM copy of something in flash -- and writing there would
    // change a copy that the next cache miss throws away, silently. The write
    // hooks reach ram_decl_at/ram_instr_at and refuse an index below the ROM
    // base, which is the only place a write can legally land.
    MC_DECLS  = 46,   // <fld>
    MC_INSTRS = 47,   // <fld>
    MC_BUFS   = 48,   // <fld>
    MC_VIEWS  = 49,   // <fld>
    MC_DECLS2 = 50,   // <fld>  ( lo hi i -- )
    MC_INSTRS2= 51,   // <fld>
    MC_BUFS2  = 52,   // <fld>
    MC_VIEWS2 = 53,   // <fld>
    MC_STS    = 54,   // <sid>  ( v -- )  a runtime-state field

    // A RUNTIME TABLE. st->io[], st->offs[], st->object[] and the rest are
    // where most of what the runtime does actually lives, and a word could not
    // reach any of them. Indexed like a state field is named: the id says WHICH
    // table, the top of the stack says which element.
    //
    // The BOUND belongs to the table, not to the caller. Every one of them has
    // a count beside it in csp_rt_t and the generated accessor checks against
    // it, so a word cannot read past a table the way C could -- reading out of
    // range answers zero rather than whatever follows in the arena.
    // SIGNED less-than. MC_LT is unsigned, which is right for an index and
    // wrong for an answer that can be -1: 0xFFFF < 0 is false as a cell and
    // true as a number, and a word holding one cannot say which it meant
    // without saying so.
    MC_SLT    = 57,   // ( a b -- flag )  signed

    // THE LONG BRANCHES. MC_JZ and MC_JMP carry one signed byte, which is the
    // common case and a byte cheaper; these carry two. Which form a jump gets
    // is decided at emission by relaxation -- see utils/gen_words.erl -- and
    // never by whoever wrote the word.
    MC_JZ16   = 58,   // <lo><hi>  signed 16-bit displacement
    MC_JMP16  = 59,

    // The two commonest literals, as one byte instead of two. Not a special
    // case anyone writes: a peephole in the generator rewrites LIT8 0 and
    // LIT8 1 into these, which it can only do because the stream is symbolic
    // until layout -- shortening it used to break every branch after it.
    MC_ZERO   = 60,
    MC_ONE    = 61,

    // INDEX(): mask the object bits off a packed declaration index. The
    // commonest phrase in the generated stream by a distance -- it was
    // LIT16 lo hi AND, four bytes, eight times -- and the mask is a compile-
    // time constant of the runtime, not something a word should be carrying.
    MC_INDEX  = 62,   // ( n -- n & INDEX_MASK )

    // AN ARRAY FIELD: one description, many elements. The field id names
    // element ZERO and the index comes off the stack, so a run of same-width
    // byte-aligned fields is reachable without one opcode -- or one switch arm
    // -- per element. Elements are whole bytes, which the layout generator
    // refuses to emit otherwise, so element k is byte + k*(bits/8).
    MC_FLDI   = 63,   // <fam><fld>  ( i k -- v )
    MC_FLDIS  = 64,   // <fam><fld>  ( v i k -- )

    MC_AGET   = 55,   // <aid>  ( i -- v )
    MC_ASET   = 56,   // <aid>  ( v i -- )
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
// BYTE-oriented, not word-oriented. Both can name the same bit, but the word
// form made the layout obey the reader: a 32-bit field had to start on a
// multiple of four, and a descriptor always read four bytes whether the record
// had them or not -- `buf` in a six-byte csp_view_t read two bytes past the
// end. A byte offset puts the constraint where it belongs (bit + bits <= 32,
// which a byte-aligned field satisfies by construction) and lets the reader
// touch only the bytes the field spans. The common case -- byte-aligned, eight
// bits or fewer -- is then one load and a mask.
typedef struct {
    uint8_t byte;    // byte offset into the record
    uint8_t bit;     // bit position within that byte, 0..7
    uint8_t bits;    // width, 1..32 (a cell is 16 bits, so MC_DECL and friends
                     // refuse anything wider; the double opcodes take it)
} mc_field_t;

// Field ids for MC_DECL are GENERATED: gen/csp_layout.h carries the MF_* enum
// and CSP_DECL_COMMON_FIELDS, both computed from utils/layout.terms. They were
// written out by hand here once, which meant two descriptions of one bit layout
// -- the exact hazard the terms file removes. The array is declared without a
// size so this header does not have to pull the generated one in.
extern const mc_field_t mc_decl_fields[];
extern const uint8_t    mc_decl_nfield;
extern const uint8_t    mc_decl_ndouble;
extern const mc_field_t mc_instr_fields[];
extern const uint8_t    mc_instr_nfield;
extern const uint8_t    mc_instr_ndouble;
extern const mc_field_t mc_buf_fields[];
extern const uint8_t    mc_buf_nfield;
extern const uint8_t    mc_buf_ndouble;
extern const mc_field_t mc_view_fields[];
extern const uint8_t    mc_view_nfield;
extern const uint8_t    mc_view_ndouble;

// Read one field out of a record already in RAM. Pure, and deliberately so:
// this is the part that can be wrong in a way nothing else notices, and a pure
// function is one a test can hammer without a runtime around it.
extern mc_cell_t mc_field_get_k(const void* rec, const mc_field_t* f,
				uint8_t k);
// Element ZERO, which is every field that is not an array.
#define mc_field_get(rec, f) mc_field_get_k((rec), (f), 0)

// The same field, written. Read-modify-write over the bytes the field spans
// and nothing else -- a neighbour sharing a byte must come back unchanged,
// which is what tests/mcsp.c checks by writing every field of a record in turn
// and reading all the others back.
extern void mc_field_set_k(void* rec, const mc_field_t* f, mc_cell_t v,
			   uint8_t k);
#define mc_field_set(rec, f, v) mc_field_set_k((rec), (f), (v), 0)

// A field WIDER than a cell has no reader of its own: it is two adjacent rows,
// low half then high, and MC_DECL2 reads tab[b] and tab[b+1]. Those rows come
// FIRST in each table, so mc_*_ndouble is the line between what may be read as
// a cell and what may not -- checked in both directions, which a width test in
// the row could only do in one.

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
    // WHERE THE MACHINE STANDS, written back before a native runs and stale
    // at every other moment. A native may call a WORD -- new_string calls
    // str_seg_stamp -- and that nested run shares these arrays. Without this
    // it would start at the top of each of them and write over the locals of
    // the word that is still on the C stack below it.
    mc_cell_t*     sp;        // data stack pointer; NULL until a native runs
    uint8_t        rp;        // return stack depth
    uint8_t        lvtop;     // first local slot NOT spoken for
    // IN, not out: how many locals the ENTRY word wants. Every other word
    // learns it from its call site (MC_CALL carries the callee's frame); the
    // one the run starts in has no call site.
    uint8_t        frame;
} mc_vm_t;

// Result codes. A bad token or a stack that ran off its end is a BUG in the
// generator, not an input error, so it stops rather than limping.
#define MC_OK          0
#define MC_E_OPCODE   -1
#define MC_E_STACK    -2
#define MC_E_BOUNDS   -3
#define MC_E_LEAF     -4
// A STEP BUDGET, and only where one is asked for. A word that loops forever
// does not fail a test, it hangs it -- breaking MC_ZERO on purpose turned
// `while (np != 0)` into a spin at 100% for five minutes, and a mis-counted
// branch displacement did the same thing earlier. Production pays a counter
// per dispatch for nothing, so this is opt-in: build with
// -DCSP_MCSP_STEPS=<n> and a runaway becomes a failure with a name.
#define MC_E_STEPS    -5

// How the domain words reach the runtime. Set once by whoever wires micro-csp
// to a csp_rt_t; left NULL, MC_DECL and MC_ND return MC_E_LEAF instead of
// dereferencing nothing. Indirect so a test can drive the machine without the
// runtime linked behind it.
// THE RECORD HOOKS, as two arrays indexed by family. What the machine needs
// per family is a hook, a field table and its length -- so those are a table
// and the sixteen field opcodes share four bodies. The named spellings below
// are what every install site uses; they are elements of these.
#define MC_FAM_DECL   0
#define MC_FAM_INSTR  1
#define MC_FAM_BUF    2
#define MC_FAM_VIEW   3

extern const void* (*mc_rd_hook[4])(void* ctx, mc_cell_t i);
// The WRITABLE record, which is never the one a read may hand back: see the
// note on MC_DECLS. NULL where the index names something that cannot be
// written -- a ROM declaration, or nothing at all.
extern void*       (*mc_wr_hook[4])(void* ctx, mc_cell_t i);

#define mc_decl_hook   mc_rd_hook[MC_FAM_DECL]
#define mc_instr_hook  mc_rd_hook[MC_FAM_INSTR]
#define mc_buf_hook    mc_rd_hook[MC_FAM_BUF]
#define mc_view_hook   mc_rd_hook[MC_FAM_VIEW]
#define mc_decl_wr     mc_wr_hook[MC_FAM_DECL]
#define mc_instr_wr    mc_wr_hook[MC_FAM_INSTR]
#define mc_buf_wr      mc_wr_hook[MC_FAM_BUF]
#define mc_view_wr     mc_wr_hook[MC_FAM_VIEW]

extern void  (*mc_state_set)(void* ctx, mc_cell_t i, mc_cell_t v);

// The runtime's tables, by id. Bounds live in the generated accessors these
// call, so an out-of-range read is zero and an out-of-range write is dropped --
// there is no pointer for a word to get wrong.
extern mc_cell_t (*mc_array_hook)(void* ctx, mc_cell_t id, mc_cell_t ix);
extern void      (*mc_array_set)(void* ctx, mc_cell_t id, mc_cell_t ix,
				 mc_cell_t v);
extern mc_cell_t   (*mc_state_hook)(void* ctx, mc_cell_t i);

extern int csp_mcsp_run(mc_vm_t* vm, uint16_t entry, mc_cell_t* result);

#endif
