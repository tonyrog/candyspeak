// The node's own console wire, tapped at both ends.
//
// Normally the serial port feeds the interpreter and the interpreter prints
// back to it. A `#buffer ... console` or `#buffer ... repl` splices into that
// wire so a rule can carry the bytes somewhere else -- over CAN to a node with
// no serial port, which is the whole point.
//
//   console   the serial port.  in = what was TYPED, out = what is SHOWN
//   repl      the interpreter.  in = what it PRINTED, out = fed in as typed
//
// A node being driven from elsewhere declares `repl`; the node with the
// keyboard declares `console`.
//
// TWO RINGS, and they are what this file is. Everything else here is four
// short functions around them.
//
// AT CSP_CONSOLE_BYTES == 0 -- the default -- there are no rings at all and
// every function below is a stub. A program declaring `console` or `repl` then
// compiles, links and runs and simply never delivers, which is the same
// contract src/csp_transport.c gives a program naming a bus the board does not
// have. A board that wants a remote console defines the size.

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "csp.h"
#include "csp_line.h"
#include "csp_print.h"

#if CSP_CONSOLE_BYTES > 0

// One ring per direction of travel, named for where the bytes CAME FROM
// (CON_KEYS and CON_OUT, in csp.h, because csp_buf_input names them too).
//
//   CON_KEYS    typed at the serial port, waiting for a rule to take them
//   CON_OUT     printed by the interpreter, waiting for a rule to take them
//
// Both are filled by something that must not block (a UART interrupt, a print
// halfway down a rule) and drained once per cycle by csp_buf_input. That is the
// whole reason they exist: see the note on blocking below.
typedef struct {
    uint8_t  buf[CSP_CONSOLE_BYTES];
    uint16_t head;                     // next byte to take
    uint16_t tail;                     // next free slot
    uint32_t lost;                     // bytes dropped, never silently
} con_ring_t;

static con_ring_t ring[2];

// Which ends a BUFFER is currently attached to, recomputed by csp_buf_input on
// every cycle from the buffer table -- so it follows a /undo that drops the
// declaration, and a rebuild that adds one.
//
// Without it the tap would record every ordinary print on any build with the
// rings compiled in, fill CON_OUT once and then count losses for the life of
// the program: a number that means nothing, reported as though it meant
// something. The cost is that whatever is printed before the first csp_buf_input
// is not captured, which is the banner and nothing else.
static uint8_t wired = 0;

// Diverted: keystrokes go to the CON_KEYS ring instead of the line editor.
// Toggled by the escape and by nothing else -- in particular NOT by declaring a
// buffer, because a node whose relaying rule is wrong must still come up with a
// usable prompt.
static uint8_t diverted = 0;

static int ring_put(con_ring_t* r, uint8_t c)
{
    uint16_t nxt = (uint16_t)((r->tail + 1) % CSP_CONSOLE_BYTES);

    // FULL: drop, and COUNT it.
    //
    // The two alternatives are both worse. Blocking here deadlocks: this is
    // called from inside csp_print_char, the ring is drained by csp_buf_input,
    // and csp_buf_input runs in the cycle -- which cannot run while a print is
    // waiting. It looks exactly like the UART busy-wait a few lines away in
    // every port, and that one is safe only because the hardware drains itself.
    //
    // Dropping SILENTLY is the other one, and it hands you a listing with a
    // hole in it that reads as valid. So the count, which /state prints.
    if (nxt == r->head) {
	r->lost++;
	return 0;
    }
    r->buf[r->tail] = c;
    r->tail = nxt;
    return 1;
}

// Up to *len bytes, and as many as there are: a stream is not a frame, so this
// takes everything it can rather than one item. What is left waits for the next
// cycle -- nothing here is ever dropped for being overtaken the way a datagram
// is, because a byte has no newer version of itself.
static uint16_t ring_take(con_ring_t* r, uint8_t* data, uint16_t max)
{
    uint16_t n = 0;

    while ((n < max) && (r->head != r->tail)) {
	data[n++] = r->buf[r->head];
	r->head = (uint16_t)((r->head + 1) % CSP_CONSOLE_BYTES);
    }
    return n;
}

void csp_con_wire(uint8_t mask) { wired = mask; }

void csp_repl_tap(char c)
{
    if (wired & (1 << CON_OUT))
	(void)ring_put(&ring[CON_OUT], (uint8_t)c);
}

// One character off the port. Returns 1 if the console took it.
static int csp_con_key(csp_rt_t* st, char c)
{
    (void)st;

    // THE ESCAPE, in front of everything. This is the one thing that must work
    // when the CandySpeak side does not: while diverted, every keystroke
    // belongs to the far end and the local prompt is unreachable, so the way
    // back cannot itself be a rule.
    if (c == (char)CSP_CONSOLE_ESCAPE) {
	diverted = !diverted;
	// Say which way it went. Two words, on the local console, because the
	// far end's output is about to stop or start and the reason should not
	// have to be guessed.
	if (diverted)
	    csp_print_line("[remote]");
	else
	    csp_print_line("[local]");
	return 1;
    }
    if (!diverted)
	return 0;                      // the line editor's, as always
    (void)ring_put(&ring[CON_KEYS], (uint8_t)c);
    return 1;
}

// What a PORT calls in place of csp_line_input: the escape and the diversion
// first, the line editor otherwise. One call site per read, and the escape is
// then written once rather than in every port.
void csp_con_input(csp_rt_t* st, char c)
{
    if (csp_con_key(st, c))
	return;
    csp_line_input(&st->line, c);
}

int csp_con_diverted(void) { return diverted; }

uint32_t csp_con_lost(void) { return ring[CON_KEYS].lost + ring[CON_OUT].lost; }

int csp_con_take(int which, uint8_t* data, uint16_t* len)
{
    uint16_t n = ring_take(&ring[which], data, *len);

    if (n == 0)
	return 0;
    *len = n;
    return 1;
}

// TR_CONSOLE out: SHOW it. Straight to csp_print_char, which is where every
// other character this node writes goes.
void csp_con_show(const uint8_t* data, uint16_t len)
{
    uint16_t i;
    for (i = 0; i < len; i++)
	csp_print_char((char)data[i]);
}

// TR_REPL out: FEED the interpreter, as if it had been typed.
//
// QUEUED, never executed here. csp_line_input hands the line to the main loop
// at its usual point; calling csp_process_line from a rule would compile a
// declaration in the middle of a cycle and rebuild the very structures that
// cycle is standing in. The arena is not re-entrant, and this is the one place
// it would be easy to forget.
void csp_con_feed(csp_rt_t* st, const uint8_t* data, uint16_t len)
{
    uint16_t i;
    for (i = 0; i < len; i++) {
	if (!csp_line_space(&st->line))
	    break;                     // back-pressure: the rest waits a cycle
	csp_line_input(&st->line, (char)data[i]);
    }
}

#else  /* no rings: the transports parse and run, and never deliver */

void csp_con_wire(uint8_t mask) { (void)mask; }
void csp_repl_tap(char c) { (void)c; }
int csp_con_diverted(void) { return 0; }
uint32_t csp_con_lost(void) { return 0; }
int csp_con_take(int which, uint8_t* data, uint16_t* len)
{
    (void)which; (void)data; (void)len;
    return 0;
}
void csp_con_show(const uint8_t* data, uint16_t len) { (void)data; (void)len; }
void csp_con_feed(csp_rt_t* st, const uint8_t* data, uint16_t len)
{
    (void)st; (void)data; (void)len;
}

#endif
