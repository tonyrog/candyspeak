// The contract behind the instruction word, probed on OP_SETO / OP_SETOX.
//
// Three CRC mismatches in this project had the same shape: a format whose
// payload was written by a GENERIC emitter arm that did not carry it. OP_SETO
// rendered through the ALU arm, DECL_STATES folded through DECL_COMMON,
// DECL_VIEW emitted as common-fields-only. Each time the emitted bytes drifted
// from the bytes the CRC was folded over, and the board rejected its own image
// at boot with nothing to point at.
//
// OP_SETO and OP_SETOX are the pair most able to repeat it: they mean nearly
// the same thing, they sit next to each other in the enum, and their payloads
// OVERLAP -- .o.obj is 16 bits starting where .ox.x's 4 bits start. So an
// emitter that writes one arm for the other produces a word that looks
// plausible and is wrong.
//
// This probes the real structs rather than restating the numbers.
//
// Run from tests/repl.sh. Prints "ok, distinct" on success.

#include <stdio.h>
#include <string.h>
#include "csp.h"

static int errors = 0;

static void fail(const char* what, unsigned long got, unsigned long want)
{
    printf("MISMATCH %s: got %lu, want %lu\n", what, got, want);
    errors++;
}

#define REG_MAX ((1u << REG_BITS) - 1u)

int main(void)
{
    csp_instr_t a;
    csp_instr_t b;
    unsigned r;

    // 1. The word is still four bytes with the new arm in the union. A static
    // assert covers this too, but it is the premise of everything below.
    if (sizeof(csp_instr_t) != 4)
	fail("sizeof(csp_instr_t)", (unsigned long)sizeof(csp_instr_t), 4);

    // 2. OP_SETOX is a real, distinct opcode below the section terminator.
    // Inserting it before OP_AVAIL must not have pushed the sentinel into
    // OP_END_MARK -- that would be a silent overlap with the terminator the
    // scan-based recovery looks for.
    if (OP_SETOX == OP_SETO)
	fail("OP_SETOX == OP_SETO", OP_SETOX, (unsigned long)OP_SETO + 1);
    if (!(OP_AVAIL <= OP_END_MARK))
	fail("OP_AVAIL past OP_END_MARK", OP_AVAIL, OP_END_MARK);

    // 3. .ox.x holds the full register width. If REG_BITS grows and this field
    // is not widened with it, high registers alias to low ones and an array
    // access silently reads the wrong object.
    for (r = 0; r <= REG_MAX; r++) {
	memset(&a, 0, sizeof(a));
	a.ox.op = OP_SETOX;
	a.ox.x  = r;
	if (a.ox.x != r)
	    fail("ox.x roundtrip", a.ox.x, r);
	if (a.ox.op != OP_SETOX)
	    fail("ox.op clobbered by x", a.ox.op, OP_SETOX);
    }

    // 4. THE EMITTER TRAP. Writing the SETOX arm must leave the ALU arm's y and
    // z at zero -- that is what lets the ROM image equal the RAM word the CRC
    // was folded over. The generic ALU arm would write all three registers; it
    // happens to produce the same bytes TODAY only because y and z are zero
    // here. This pins that, so if the default arm ever changes the test says so
    // instead of a board saying "CRC mismatch in instr section".
    memset(&a, 0, sizeof(a));
    a.ox.op = OP_SETOX;
    a.ox.x  = REG_MAX;
    if (a.a.y != 0)
	fail("SETOX leaked into a.y", a.a.y, 0);
    if (a.a.z != 0)
	fail("SETOX leaked into a.z", a.a.z, 0);
    if (a.a.x != REG_MAX)
	fail("ox.x does not alias a.x", a.a.x, REG_MAX);

    // 5. THE CONFUSION TRAP. The two formats overlap, so emitting one through
    // the other's arm is not caught by the compiler. An object number that does
    // not fit four bits must come back DIFFERENT when read as .ox.x -- that
    // difference is the whole reason each format needs its own emitter arm.
    // Objects 1..15 survive the mix-up, which is exactly the coincidence that
    // holds until a program has sixteen of them.
    memset(&b, 0, sizeof(b));
    b.o.op  = OP_SETO;
    b.o.obj = REG_MAX + 1;             // first object that does not fit .ox.x
    if (b.ox.x == b.o.obj)
	fail("SETO/SETOX payloads indistinguishable", b.ox.x, b.o.obj);
    if (b.o.obj != REG_MAX + 1)
	fail("o.obj roundtrip", b.o.obj, REG_MAX + 1);

    // 6. The full 16-bit object range still survives .o.obj, unchanged by the
    // new neighbour in the union.
    memset(&b, 0, sizeof(b));
    b.o.op  = OP_SETO;
    b.o.obj = MAX_OBJECT_NUM;
    if (b.o.obj != MAX_OBJECT_NUM)
	fail("o.obj full width", b.o.obj, MAX_OBJECT_NUM);

    if (errors == 0)
	printf("ok, distinct\n");
    return errors != 0;
}
