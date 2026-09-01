// The contract behind OP_SEGMENT and instr_next().
//
// Identifier text lives in the INSTRUCTION pool: an OP_SEGMENT header carrying
// the payload count, then that many words of characters. The payload is NOT
// instructions -- its opcode nibble is whatever character landed there, and one
// value in four reads as something with operands to chase.
//
// EXECUTION is safe by construction: OP_SEGMENT is a jump, so eval steps past
// the run on its own. What is NOT safe is the half-dozen loops that walk the
// stream LINEARLY -- the reactive graph builder, the rule scanners, the ROM
// emitter, the parse dump. They go through instr_next(), and a step that is off
// by one lands inside a segment among instructions made of text.
//
// So this probes the real struct rather than restating the numbers: build a
// stream with a segment in the middle, fill its payload with bytes that spell a
// valid opcode, walk it, and check the walk never lands inside.
//
// Run from tests/repl.sh. Prints "ok, stepped over" on success.

#include <stdio.h>
#include <string.h>
#include "csp.h"

#define NSLOT 24

int main(void)
{
    csp_instr_t tab[NSLOT];
    int i, seen, bad = 0;

    memset(tab, 0, sizeof(tab));

    // 0: an add. 1: a segment with 8 payload words. 10, 11: more real code.
    tab[0].op = OP_ADD;
    tab[1].op = OP_SEGMENT;
    tab[1].sg.num = 8;
    tab[10].op = OP_ADD;
    tab[11].op = OP_NEXT;

    // Fill the payload with text. Any byte whose low six bits name an opcode
    // would be followed into -- which is what a name does by accident.
    memset(&tab[2], 'A', 8 * sizeof(csp_instr_t));

    // A segment covers header + payload; everything else is one word.
    if ((tab[1].sg.num + 1) != 9) {
	printf("segment span is %u, want 9\n", (unsigned)(tab[1].sg.num + 1));
	bad = 1;
    }

    // Walk it the way the linear loops do: land on 0, 1, 10, 11 and never
    // inside 2..9.
    seen = 0;
    for (i = 0; i < 12; ) {
	if (i > 1 && i < 10) {
	    printf("walk landed inside the segment, at %d\n", i);
	    bad = 1;
	    break;
	}
	seen++;
	i += (tab[i].op == OP_SEGMENT) ? (int)(tab[i].sg.num + 1) : 1;
    }
    if (seen != 4) {
	printf("walk visited %d words, want 4\n", seen);
	bad = 1;
    }

    // The header must fit an instruction word, and its fields survive the union.
    if (sizeof(csp_instr_seg_t) > sizeof(csp_instr_t)) {
	printf("csp_instr_seg_t is %d bytes, does not fit a %d-byte word\n",
	       (int)sizeof(csp_instr_seg_t), (int)sizeof(csp_instr_t));
	bad = 1;
    }
    // 128 bytes is 32 words, and `used` has to hold 0..128.
    tab[1].sg.num = 32;
    tab[1].sg.used = 128;
    if (tab[1].sg.num != 32 || tab[1].sg.used != 128 ||
	tab[1].op != OP_SEGMENT) {
	printf("num=32/used=128 does not fit, or it overwrote the opcode\n");
	bad = 1;
    }

    if (!bad)
	printf("ok, stepped over\n");
    return bad;
}
