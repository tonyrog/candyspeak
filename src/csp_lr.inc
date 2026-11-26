// The LR engine, a value beside each state. stk[i] is
// the state below symbol i, val[i] its value; s the state on top.
//
// Not a file of its own: included by csp_parse.c, with RO, RB and RW defined
// as the compiler's ROM access, and by tests/lr/lrtest.c. A .c here would be
// compiled once more by the Arduino sketch, which takes every .c in src/.
#include <stdint.h>
#include "csp_lr.h"

#ifdef RO
// the includer's
#elif defined(__AVR__)
#include <avr/pgmspace.h>
#define RO PROGMEM
#define RB(p) pgm_read_byte(p)
#define RW(p) pgm_read_word(p)
#else
#define RO
#define RB(p) (*(const uint8_t*) (p))
#define RW(p) (*(const uint16_t*) (p))
#endif

#include "csp_lr_tab.h"

#ifdef LR_PEAK
int lr_peak;
#define PEAK(sp) do { if ((sp) > lr_peak) lr_peak = (sp); } while (0)
#else
#define PEAK(sp)
#endif

// search one row and its parents; the entry or 0
static const uint8_t* row_find(const uint8_t* r, uint8_t tok)
{
    for (;;) {
	uint8_t n = RB(r);
	const uint8_t* e = r + 3;

	while (n--) {
	    if ((RB(e) & 0x7f) == tok)
		return e;
	    e += 2;
	}
	if (RB(r + 2) == 255)
	    return 0;
	r = lr_rows + RW(&lr_parent_off[RB(r + 2)]);
    }
}

// goto on nonterminal nt from state s: a state, or NSTATES + a class
static uint16_t lr_goto(uint8_t s, uint8_t nt)
{
    const uint8_t* g = lr_gotos + RW(&lr_goto_off[nt]);
    uint8_t n = RB(g);
    const uint8_t* e = g + 3;

    while (n--) {
	if (RB(e) == s)
	    return RB(e + 1) | (RB(e + 2) << 8);
	e += 3;
    }
    return RB(g + 1) | (RB(g + 2) << 8);
}

#define PUSH(x, v) do {				\
	if (sp >= LR_DEPTH)			\
	    return LR_DEEP;			\
	stk[sp] = (x);				\
	val[sp++] = (v);			\
	PEAK(sp);				\
    } while (0)

int lr_parse(lr_next_t next, lr_reduce_t reduce, void* ctx, lr_val_t* result)
{
    uint8_t stk[LR_DEPTH];
    lr_val_t val[LR_DEPTH];
    uint8_t sp = 0;
    uint8_t s = 0;
    uint8_t c, n, g;
    uint16_t t;
    int tok;
    lr_val_t tv, v;
    const uint8_t* r;
    const uint8_t* e;

#ifdef LR_PEAK
    lr_peak = 0;
#endif
    if ((tok = next(ctx, &tv)) < 0)
	return LR_LEX;
    for (;;) {
	r = lr_rows + RW(&lr_row_off[s]);
	e = row_find(r, (uint8_t) tok);
	if (e != 0) {
	    PUSH(s, tv);
	    c = RB(e + 1);
	    if ((RB(e) & 0x80) == 0) {
		s = c;              // shift
		if ((tok = next(ctx, &tv)) < 0)
		    return LR_LEX;
		continue;
	    }
	    if (c == LR_ACCEPT) {   // $end shifted: the program below it
		*result = val[sp - 2];
		return LR_OK;
	    }
	    if ((tok = next(ctx, &tv)) < 0)
		return LR_LEX;
	}
	else {
	    c = RB(r + 1);
	    if (c == 0)
		return LR_SYNTAX;
	    c--;
	    if (RB(&lr_len[c]) == 0) {
		v = reduce(ctx, RB(&lr_act[c]), 0, 0);
		g = s;              // empty rule: reduce in place
		goto do_goto;
	    }
	}
	// reduce class c, its symbols are on top of the stack
	for (;;) {
	    n = RB(&lr_len[c]);
	    sp -= n;
	    v = reduce(ctx, RB(&lr_act[c]), &val[sp], n);
	    g = stk[sp];
	do_goto:
	    PUSH(g, v);
	    t = lr_goto(g, RB(&lr_lhs[c]));
	    if (t < LR_NSTATES)
		break;
	    c = t - LR_NSTATES;     // goto-reduce: completes another rule
	}
	s = t;
    }
}
