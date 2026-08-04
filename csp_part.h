#ifndef __CSP_PART_H__
#define __CSP_PART_H__

// Where every `.part` of a value SLOT lives: bit position and width inside the
// 32-bit value_t word. THIS TABLE IS THE DEFINITION -- csp_dio_get_part and
// csp_dio_set_part read it instead of naming bitfields.
//
// WHY A TABLE. There used to be six functions (csp_digital/analog/timer_
// get/set_part), each a switch over csp_part_t, each case naming one bitfield
// in one union arm. 762 bytes on AVR to express what is a shift and a mask,
// plus a four-way dispatch in csp_dio_*_part whose tail-call branches each got
// their own epilogue (72 `pop` against 12 `push` in one function).
//
// WHY IT IS SAFE TO HAND-WRITE OFFSETS. Normally it is not: the parts are
// bitfields in different arms of a union, so a wrong number here would corrupt
// data instead of failing to compile. That is why tests/part_layout.c PROBES
// the actual structs -- it zeroes a value_t, sets one field to all ones, and
// reads the word back, so the position and width come from the compiler's own
// layout. It then checks every row below against that probe. Change PIN_BITS,
// reorder a struct, add a field: the test fails and names the row.
//
// WHAT IS NOT HERE. A string is not a bitfield layout (its slot holds a whole
// position -- see csp_string_get_part), and .dir/.rx/.tx/.id/.dlc live in
// csp_buf_t, not in the value word (see csp_view_get_part).

#include "csp.h"

// Layout id. V_TIMER/V_DIGITAL/V_ANALOG are consecutive, so the id is a
// subtraction and the row index is a shift -- no multiply on AVR.
#define PL_TIMER    0
#define PL_DIGITAL  1
#define PL_ANALOG   2
#define PL_COUNT    3

CSP_STATIC_ASSERT(V_DIGITAL == V_TIMER + 1, "layout id assumes V_TIMER..V_ANALOG are consecutive");
CSP_STATIC_ASSERT(V_ANALOG  == V_TIMER + 2, "layout id assumes V_TIMER..V_ANALOG are consecutive");

// Row = pos:5 | width-code:3. Code 0 means "this type has no such part", and
// since it makes the whole byte 0 it cannot collide with a real row: the only
// part at position 0 is timer .period, whose code is PLC_28.
#define PLC_NONE 0
#define PLC_1    1
#define PLC_2    2
#define PLC_4    3
#define PLC_7    4
#define PLC_16   5
#define PLC_28   6
#define PLC_32   7
#define PL(pos,code) ((uint8_t)((pos) | ((code) << 5)))

// Masks are looked up, not computed: (1<<w)-1 needs a variable 32-bit shift,
// which gcc turns into a loop on AVR. This leaves exactly one shift per access.
static const uint32_t csp_pl_mask[8] RODATA = {
    0, 0x1u, 0x3u, 0xFu, 0x7Fu, 0xFFFFu, 0x0FFFFFFFu, 0xFFFFFFFFu
};

// The stride is 1 << PART_BITS so the row index is (lay << PART_BITS) | part.
#define PL_STRIDE (1 << PART_BITS)

static const uint8_t csp_part_loc[PL_COUNT * PL_STRIDE] RODATA = {
    // --- tvalue_t ------------------------------------------------------------
    // .running is deliberately absent: it is runtime state the timer owns, not
    // a part a rule may name.
    [(PL_TIMER   << PART_BITS) | PART_VAL]      = PL(31, PLC_1),
    [(PL_TIMER   << PART_BITS) | PART_PERIOD]   = PL( 0, PLC_28),
    [(PL_TIMER   << PART_BITS) | PART_FIRED]    = PL(29, PLC_1),

    // --- dvalue_t ------------------------------------------------------------
    // .val is ONE bit here although the struct field is 16. Reads always masked
    // to 1 (csp_digital_get_part and csp_dio_get_val_part both did `& 1`), so
    // nothing observable changes; the write now stops at bit 16 instead of
    // filling all sixteen. If shift-in ever lands, this row becomes PLC_16.
    [(PL_DIGITAL << PART_BITS) | PART_VAL]      = PL(16, PLC_1),
    [(PL_DIGITAL << PART_BITS) | PART_PIN]      = PL( 0, PLC_7),
    [(PL_DIGITAL << PART_BITS) | PART_PORT]     = PL( 7, PLC_4),
    [(PL_DIGITAL << PART_BITS) | PART_DIR]      = PL(11, PLC_2),
    [(PL_DIGITAL << PART_BITS) | PART_PULLUP]   = PL(13, PLC_1),
    [(PL_DIGITAL << PART_BITS) | PART_PULLDOWN] = PL(14, PLC_1),

    // --- avalue_t ------------------------------------------------------------
    // No .endian: byte order that means something lives in csp_view_t.endian
    // and is answered from the declaration (see the avalue_t comment).
    [(PL_ANALOG  << PART_BITS) | PART_VAL]      = PL(16, PLC_16),
    [(PL_ANALOG  << PART_BITS) | PART_PIN]      = PL( 0, PLC_7),
    [(PL_ANALOG  << PART_BITS) | PART_PORT]     = PL( 7, PLC_4),
    [(PL_ANALOG  << PART_BITS) | PART_DIR]      = PL(11, PLC_2),
    [(PL_ANALOG  << PART_BITS) | PART_PWM]      = PL(13, PLC_1),
};

// Position of the `cfg` bit per layout, 0 for a layout that has none. cfg is
// NOT a per-row flag: writing ANY part except .val is a configuration change
// and writing .val never is -- that held for all eleven writable parts, so the
// rule is one line of code instead of a bit in every row. (Bit 0 is a pin
// number in both layouts that have a cfg, so 0 is free as "none".)
static const uint8_t csp_part_cfg[PL_COUNT] RODATA = {
    [PL_TIMER] = 0, [PL_DIGITAL] = 15, [PL_ANALOG] = 14
};

// The layout id for a value type, or PL_COUNT (no rows) if it has none.
// A macro rather than a function on purpose: it is one subtraction, and the two
// users need it as a plain value -- csp_part_row used to hand it back through
// an out-parameter, which forces the caller to give it a stack address and read
// it back, and stops both callers from being LEAF functions. On AVR a leaf pays
// no prologue at all (it may use the call-clobbered registers freely), so the
// out-parameter was costing far more than the subtraction it saved.
#define CSP_PART_LAY(vt)  ((uint8_t)((vt) - V_TIMER))

// Row for (vt, part); 0 when this type has no such part.
static uint8_t csp_part_row(vtype_t vt, csp_part_t part)
{
    uint8_t l = CSP_PART_LAY(vt);
    if (l >= PL_COUNT)
	return 0;
    return ro_byte(&csp_part_loc[((uint16_t)l << PART_BITS) |
				 CSP_MASK(part, PART_BITS)]);
}

// Read `part` out of a value slot. A part this type does not have reads 0 --
// the old per-type switches left *vp UNTOUCHED on their default case, which
// handed the caller whatever was in the register (or, at compile-time fold
// time, an uninitialised local).
static void csp_part_get(const value_t* slot, vtype_t vt, csp_part_t part,
			 value_t* vp)
{
    uint8_t r = csp_part_row(vt, part);

    if (r == 0) {
	vp->u = 0;
	return;
    }
    // Every part is an unsigned bitfield -- no sign extension anywhere, unlike
    // csp_heap_get. A signed part added later would need it explicitly.
    vp->u = (slot->u >> (r & 31)) & ro_dword(&csp_pl_mask[r >> 5]);
}

// Write `part` into a value slot, leaving the surrounding bits alone. A part
// this type does not have is ignored, as before.
static void csp_part_set(value_t* slot, vtype_t vt, csp_part_t part, value_t v)
{
    uint8_t r = csp_part_row(vt, part);
    uint8_t pos, cfg;
    uint32_t m;

    if (r == 0)
	return;
    pos = (uint8_t)(r & 31);
    m = ro_dword(&csp_pl_mask[r >> 5]) << pos;
    slot->u = (slot->u & ~m) | ((v.u << pos) & m);
    if (CSP_MASK(part, PART_BITS) != PART_VAL) {
	cfg = ro_byte(&csp_part_cfg[CSP_PART_LAY(vt)]);
	if (cfg)
	    slot->u |= ((uint32_t)1 << cfg);
    }
}

#endif
