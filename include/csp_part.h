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
// NO HAND-WRITTEN OFFSETS. They were hand-written once and they drifted: `cfg`
// went in at the FRONT of dvalue_t and avalue_t, which moved every row here by
// one bit, and nothing failed to compile -- `Sensor.pin` simply read 10 where
// the pin was 5. The positions now come from utils/layout.terms through
// gen/csp_layout.h (MFV_<ARM>_<FIELD>_POS and _BITS), the same description the
// accessors and the micro-csp field table are built from, so a field added
// ahead of another moves this table with it.
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

// A row from the generated description: position and width of one field of one
// arm of value_t. The width code is a lookup into csp_pl_mask, so it is chosen
// here rather than stored -- every width value_t uses has a code, and a new one
// without a mask lands on PLC_32 and reads too much, which is why PLV_CODE
// spells them all out instead of ending in a default.
#define PLV_CODE(w) ((w) == 1 ? PLC_1 : (w) == 2 ? PLC_2 : (w) == 4 ? PLC_4 : \
		     (w) == 7 ? PLC_7 : (w) == 16 ? PLC_16 : \
		     (w) == 28 ? PLC_28 : PLC_32)
#define PLV(F) PL(MFV_##F##_POS, PLV_CODE(MFV_##F##_BITS))

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
    [(PL_TIMER   << PART_BITS) | PART_VAL]      = PLV(T_VAL),
    [(PL_TIMER   << PART_BITS) | PART_PERIOD]   = PLV(T_PERIOD),
    [(PL_TIMER   << PART_BITS) | PART_FIRED]    = PLV(T_FIRED),

    // --- dvalue_t ------------------------------------------------------------
    // .val is ONE bit. If shift-in ever lands, widen it in layout.terms and
    // this row follows.
    [(PL_DIGITAL << PART_BITS) | PART_VAL]      = PLV(D_VAL),
    [(PL_DIGITAL << PART_BITS) | PART_PIN]      = PLV(D_PIN),
    [(PL_DIGITAL << PART_BITS) | PART_PORT]     = PLV(D_PORT),
    [(PL_DIGITAL << PART_BITS) | PART_DIR]      = PLV(D_DIR),
    [(PL_DIGITAL << PART_BITS) | PART_PULLUP]   = PLV(D_PULLUP),
    [(PL_DIGITAL << PART_BITS) | PART_PULLDOWN] = PLV(D_PULLDOWN),
    // An interrupt trigger, taken off val -- see dvalue_t.
    [(PL_DIGITAL << PART_BITS) | PART_FIRED]    = PLV(D_FIRED),

    // --- avalue_t ------------------------------------------------------------
    // No .endian: byte order that means something lives in csp_view_t.endian
    // and is answered from the declaration (see the avalue_t comment).
    [(PL_ANALOG  << PART_BITS) | PART_VAL]      = PLV(A_VAL),
    [(PL_ANALOG  << PART_BITS) | PART_PIN]      = PLV(A_PIN),
    [(PL_ANALOG  << PART_BITS) | PART_PORT]     = PLV(A_PORT),
    [(PL_ANALOG  << PART_BITS) | PART_DIR]      = PLV(A_DIR),
    [(PL_ANALOG  << PART_BITS) | PART_PWM]      = PLV(A_PWM),
    // An interrupt trigger. The spare bit avalue_t had, below val.
    [(PL_ANALOG  << PART_BITS) | PART_FIRED]    = PLV(A_FIRED),
};

// Position of the `cfg` bit per layout, PLUS ONE, so that 0 means "this layout
// has none". Not the position itself: cfg is bit 0 of both layouts that have
// one, which is the same number a timer would have to say it has no cfg at all.
//
// cfg is NOT a per-row flag: writing ANY part except .val is a configuration
// change and writing .val never is -- that holds for all eleven writable parts,
// so the rule is one line of code instead of a bit in every row.
//
// PART_FIRED is the twelfth and it is the exception: the sweep sets and clears
// it every cycle, so treating a write as a configuration change would re-apply
// the pin on every edge. csp_dio_set_part excludes it by name.
static const uint8_t csp_part_cfg[PL_COUNT] RODATA = {
    [PL_TIMER] = 0,
    [PL_DIGITAL] = MFV_D_CFG_POS + 1,
    [PL_ANALOG]  = MFV_A_CFG_POS + 1
};

// The layout id for a value type, or PL_COUNT (no rows) if it has none.
// A macro rather than a function on purpose: it is one subtraction, and the two
// users need it as a plain value -- csp_part_row used to hand it back through
// an out-parameter, which forces the caller to give it a stack address and read
// it back, and stops both callers from being LEAF functions. On AVR a leaf pays
// no prologue at all (it may use the call-clobbered registers freely), so the
// out-parameter was costing far more than the subtraction it saved.
// MASKED to TYPE_BITS. A cfg vtype may carry flags above the type (CFG_SIGNED),
// and without the mask `vt - V_TIMER` lands past PL_COUNT -- csp_part_row then
// answers 0 for every part, so `AccX.pin` reads as zero and nothing says why.
#define CSP_PART_LAY(vt)  ((uint8_t)(CSP_MASK((vt), TYPE_BITS) - V_TIMER))

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
    // PART_FIRED joins PART_VAL in not being a configuration change: the event
    // sweep writes it every cycle, and marking cfg there would have the board
    // re-apply the pin on every edge.
    if ((CSP_MASK(part, PART_BITS) != PART_VAL) &&
	(CSP_MASK(part, PART_BITS) != PART_FIRED)) {
	cfg = ro_byte(&csp_part_cfg[CSP_PART_LAY(vt)]);
	if (cfg)
	    slot->u |= ((uint32_t)1 << (cfg - 1));
    }
}

#endif
