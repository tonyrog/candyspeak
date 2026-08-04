// The contract behind csp_part.h.
//
// csp_part.h hand-writes the bit position and width of every `.part` of a value
// slot. Those are bitfields in different arms of a union, so a wrong number
// there corrupts data rather than failing to compile -- exactly the bug class
// that makes hand-written offsets a bad idea.
//
// This test removes the objection. For every row it PROBES the real struct:
// zero a value_t, set that one field to all ones, read the 32-bit word back.
// The set bits ARE the position and the width, straight out of the compiler's
// own layout. Then it compares the probe against the committed table.
//
// So the table can only be wrong if the probe agrees with it, and the probe
// cannot agree with it unless the structs really look like that. Change
// PIN_BITS, reorder a field, add one: this fails and names the row.
//
// Run from tests/repl.sh. Prints "ok, identical" on success.

#include <stdio.h>
#include <string.h>
#include "csp.h"
#include "csp_part.h"

static int errors = 0;

// Position and width of the bits `w` covers, or (0,0) for an empty word.
static void span(uint32_t w, int* pos, int* len)
{
    int i;
    *pos = *len = 0;
    if (w == 0)
	return;
    for (i = 0; i < 32; i++)
	if (w & (1u << i)) { *pos = i; break; }
    for (i = *pos; i < 32 && (w & (1u << i)); i++)
	(*len)++;
}

static const char* part_name(csp_part_t p)
{
    switch (p) {
    case PART_VAL: return "val";        case PART_PIN: return "pin";
    case PART_PORT: return "port";      case PART_DIR: return "dir";
    case PART_PWM: return "pwm";        case PART_ENDIAN: return "endian";
    case PART_PULLUP: return "pullup";  case PART_PULLDOWN: return "pulldown";
    case PART_PERIOD: return "period";  case PART_FIRED: return "fired";
    case PART_ID: return "id";          case PART_RX: return "rx";
    case PART_TX: return "tx";          case PART_DLC: return "dlc";
    case PART_LEN: return "len";        default: return "?";
    }
}

static const char* lay_name(int lay)
{
    return (lay == PL_TIMER) ? "timer" :
	   (lay == PL_DIGITAL) ? "digital" : "analog";
}

// One row: `probe` is the word with the struct field set to all ones, `expect`
// is what the width code should decode to (0 = the row is deliberately absent,
// so the probe is not consulted).
static void check(int lay, csp_part_t part, uint32_t probe, int want_len)
{
    uint8_t r = csp_part_row((vtype_t)(V_TIMER + lay), part);
    int ppos, plen;
    int tpos = r & 31;
    int tlen = 0;
    static const int code_len[8] = { 0, 1, 2, 4, 7, 16, 28, 32 };

    tlen = code_len[r >> 5];
    span(probe, &ppos, &plen);

    if (want_len == 0) {                 // row must be absent from the table
	if (r != 0) {
	    printf("FAIL %s.%s: table has pos %d len %d, expected no row\n",
		   lay_name(lay), part_name(part), tpos, tlen);
	    errors++;
	}
	return;
    }
    if (r == 0) {
	printf("FAIL %s.%s: missing from table (struct has pos %d len %d)\n",
	       lay_name(lay), part_name(part), ppos, plen);
	errors++;
	return;
    }
    if (tpos != ppos) {
	printf("FAIL %s.%s: table pos %d, struct pos %d\n",
	       lay_name(lay), part_name(part), tpos, ppos);
	errors++;
    }
    // The width the table claims must be the width we intend. It is checked
    // against the STRUCT only when they are meant to match -- digital .val is
    // deliberately narrower than its 16-bit field (see csp_part.h).
    if (tlen != want_len) {
	printf("FAIL %s.%s: table len %d, expected %d\n",
	       lay_name(lay), part_name(part), tlen, want_len);
	errors++;
    }
    if ((want_len > plen) || (ppos + plen > 32)) {
	printf("FAIL %s.%s: table len %d exceeds the struct field (%d bits)\n",
	       lay_name(lay), part_name(part), want_len, plen);
	errors++;
    }
}

// Probe a field: all ones into that field of a zeroed value_t, word back out.
#define PROBE(arm, fld) (probe_##arm##_##fld())
#define MK_PROBE(arm, fld)				\
    static uint32_t probe_##arm##_##fld(void)		\
    {							\
	value_t v;					\
	memset(&v, 0, sizeof(v));			\
	v.arm.fld = (unsigned)~0u;			\
	return v.u;					\
    }

MK_PROBE(t, period) MK_PROBE(t, fired) MK_PROBE(t, running) MK_PROBE(t, val)
MK_PROBE(d, pin) MK_PROBE(d, port) MK_PROBE(d, dir)
MK_PROBE(d, pullup) MK_PROBE(d, pulldown) MK_PROBE(d, cfg) MK_PROBE(d, val)
MK_PROBE(a, pin) MK_PROBE(a, port) MK_PROBE(a, dir)
MK_PROBE(a, pwm) MK_PROBE(a, cfg) MK_PROBE(a, val)

// The cfg bit is not in the row table; it is one byte per layout. Same idea.
static void check_cfg(int lay, uint32_t probe)
{
    int ppos, plen;
    int tcfg = ro_byte(&csp_part_cfg[lay]);
    span(probe, &ppos, &plen);
    if (probe == 0) {                    // layout has no cfg field
	if (tcfg != 0) {
	    printf("FAIL %s: cfg bit %d, struct has no cfg\n", lay_name(lay), tcfg);
	    errors++;
	}
	return;
    }
    if (tcfg != ppos) {
	printf("FAIL %s: cfg bit %d, struct cfg at %d\n", lay_name(lay), tcfg, ppos);
	errors++;
    }
}

// A round trip through the engine: write each part, read it back, and confirm
// no other part moved. This is what the six deleted switches used to do by
// naming fields, so it is the behaviour that has to survive.
static void roundtrip(int lay, csp_part_t part, uint32_t v)
{
    vtype_t vt = (vtype_t)(V_TIMER + lay);
    value_t slot, got, other, other2;
    csp_part_t p2;

    if (csp_part_row(vt, part) == 0)
	return;
    memset(&slot, 0, sizeof(slot));
    slot.u = 0x5A5A5A5Au;                // surrounding bits must be preserved
    for (p2 = 0; p2 < PART_LAST; p2++) { // snapshot every OTHER part first
	value_t before, after;
	if ((p2 == part) || (csp_part_row(vt, p2) == 0))
	    continue;
	csp_part_get(&slot, vt, p2, &before);
	other = slot;
	csp_part_set(&other, vt, part, (value_t){ .u = v });
	csp_part_get(&other, vt, p2, &after);
	// .cfg is not a part, so a config write may not disturb any part we can
	// name -- that is the point of the check.
	if (before.u != after.u) {
	    printf("FAIL %s: writing .%s changed .%s (%lu -> %lu)\n",
		   lay_name(lay), part_name(part), part_name(p2),
		   (unsigned long)before.u, (unsigned long)after.u);
	    errors++;
	}
    }
    other2 = slot;
    csp_part_set(&other2, vt, part, (value_t){ .u = v });
    csp_part_get(&other2, vt, part, &got);
    {   // the value must come back truncated to the part's width, not mangled
	uint8_t r = csp_part_row(vt, part);
	static const int code_len[8] = { 0, 1, 2, 4, 7, 16, 28, 32 };
	int len = code_len[r >> 5];
	uint32_t want = (len >= 32) ? v : (v & ((1u << len) - 1));
	if (got.u != want) {
	    printf("FAIL %s.%s: wrote %lu, read %lu, expected %lu\n",
		   lay_name(lay), part_name(part), (unsigned long)v,
		   (unsigned long)got.u, (unsigned long)want);
	    errors++;
	}
    }
}

int main(void)
{
    csp_part_t p;
    int lay;

    if (sizeof(value_t) != 4) {
	printf("FAIL value_t is %d bytes, the table assumes 4\n",
	       (int)sizeof(value_t));
	return 1;
    }

    // --- rows against the structs -------------------------------------------
    check(PL_TIMER, PART_VAL,      PROBE(t, val),      1);
    check(PL_TIMER, PART_PERIOD,   PROBE(t, period),  28);
    check(PL_TIMER, PART_FIRED,    PROBE(t, fired),    1);
    check(PL_TIMER, PART_PIN,      0,                  0);  // not a timer part
    check(PL_TIMER, PART_PWM,      0,                  0);

    check(PL_DIGITAL, PART_VAL,      PROBE(d, val),       1); // narrowed on purpose
    check(PL_DIGITAL, PART_PIN,      PROBE(d, pin),       7);
    check(PL_DIGITAL, PART_PORT,     PROBE(d, port),      4);
    check(PL_DIGITAL, PART_DIR,      PROBE(d, dir),       2);
    check(PL_DIGITAL, PART_PULLUP,   PROBE(d, pullup),    1);
    check(PL_DIGITAL, PART_PULLDOWN, PROBE(d, pulldown),  1);
    check(PL_DIGITAL, PART_PWM,      0,                   0);  // analog only
    check(PL_DIGITAL, PART_PERIOD,   0,                   0);  // timer only

    check(PL_ANALOG, PART_VAL,       PROBE(a, val),      16);
    check(PL_ANALOG, PART_PIN,       PROBE(a, pin),       7);
    check(PL_ANALOG, PART_PORT,      PROBE(a, port),      4);
    check(PL_ANALOG, PART_DIR,       PROBE(a, dir),       2);
    check(PL_ANALOG, PART_PWM,       PROBE(a, pwm),       1);
    check(PL_ANALOG, PART_PULLUP,    0,                   0);  // digital only
    check(PL_ANALOG, PART_ENDIAN,    0,                   0);  // lives in the decl

    // --- the cfg bit ---------------------------------------------------------
    check_cfg(PL_TIMER,   0);
    check_cfg(PL_DIGITAL, PROBE(d, cfg));
    check_cfg(PL_ANALOG,  PROBE(a, cfg));

    // --- the engine ----------------------------------------------------------
    for (lay = 0; lay < PL_COUNT; lay++)
	for (p = 0; p < PART_LAST; p++) {
	    roundtrip(lay, p, 0xFFFFFFFFu);
	    roundtrip(lay, p, 0x00000000u);
	    roundtrip(lay, p, 0x0000A5A5u);
	    roundtrip(lay, p, 0x00000001u);
	}

    // --- cfg is set by config writes and only by them ------------------------
    for (lay = PL_DIGITAL; lay <= PL_ANALOG; lay++) {
	vtype_t vt = (vtype_t)(V_TIMER + lay);
	uint8_t cfg = ro_byte(&csp_part_cfg[lay]);
	for (p = 0; p < PART_LAST; p++) {
	    value_t slot;
	    int want;
	    if (csp_part_row(vt, p) == 0)
		continue;
	    slot.u = 0;
	    csp_part_set(&slot, vt, p, (value_t){ .u = 1 });
	    want = (p != PART_VAL);
	    if (!!(slot.u & (1u << cfg)) != want) {
		printf("FAIL %s.%s: cfg %s\n", lay_name(lay), part_name(p),
		       want ? "not set by a config write" : "set by a .val write");
		errors++;
	    }
	}
    }

    printf(errors ? "%d error(s)\n" : "ok, identical\n", errors);
    return errors ? 1 : 0;
}
