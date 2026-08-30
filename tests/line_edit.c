// The line editor, driven a byte at a time.
//
// It cannot be tested through a pipe. Everything a pipe holds is available at
// once, so the reader drains past the newline and the rest of the input lands
// in the paste QUEUE -- where cursor keys and history are deliberately off, or
// a recall would expand the line under the read cursor the re-feed is walking.
// Which is correct, and which means a `^P` sent down a pipe is ignored by
// design and proves nothing.
//
// So: call csp_line_input directly, one byte at a time, exactly as a serial
// port delivers them.
//
// TWO things are checked, and both have to agree. The BUFFER is what the parser
// will be handed. The SCREEN -- kept by the little terminal emulator below --
// is what the person typing is looking at, rebuilt from the backspaces and
// spaces the editor printed. Neither alone is the editor working: a line that
// parses right and reads as something else is a bug you find by squinting at a
// serial console, which is exactly the way not to find it.

#include "csp.h"

#include <stdio.h>
#include <string.h>
#include <ctype.h>

#define CTRL(x) ((x) - 64)
#define ESCAPE          27
#define DELETE          127
#define SPACE           32
#define BACKSPACE       '\b'
#define NEWLINE         '\n'
#define CARRIAGE_RETURN '\r'
#define BELL            '\a'

static csp_line_t st;
static char linebuf[128];
static char histbuf[256];

#define TERMBUF_SIZE 130
static char term_buf[TERMBUF_SIZE];
int term_pos  = 0;
int term_line = 0;
int term_arg  = -1;
int term_esc  = 0;
int term_line_size = 0;

// The screen, as far as one line of it goes.
//
// A terminal has no idea what a "line of text" is. It has a cursor and a grid of
// cells, and only three things move the cursor: printing into the cell under it,
// backspacing off it, and a newline. So that is all this models -- what the
// editor CANNOT do is the point, because the editor emits nothing else. When it
// shortens a line it does not erase anything, it prints spaces over the tail;
// what the reader then sees as "the line" is what is left after those trailing
// spaces, which is term()'s business and not this function's.
//
// Only the CURRENT line is kept. A newline starts the next one over the top of
// it: everything a test asks about happens on the line being edited, and holding
// the scrollback would only mean every check had to spell out the two lines
// above the one it cares about.
int csp_print_char(char c)
{
#ifdef DEBUG_POS
    if (isprint(c))
	printf("CHAR '%c' term_pos=%d, term_line_size=%d\n",
	       c, term_pos, term_line_size);
    else
	printf("CHAR '0x%02x' term_pos=%d, term_line_size=%d\n",
	       c, term_pos, term_line_size);
#endif
    switch(term_esc) {
    case 0:
	switch(c) {
	// Moves the cursor left. That is ALL it does -- it does not erase the
	// cell it lands on and it does not shorten the line. Tracking a length
	// here is what made the emulator disagree with every history case: the
	// editor's own rub-out is BACKSPACE, then a space, then BACKSPACE
	// again, and a backspace that shortens the line undoes the space that
	// was the whole point of printing it.
	case BACKSPACE:
	    if (term_pos > 0)
		term_pos--;
	    break;
	case BELL: break;
	case ESCAPE: term_esc = 1; break;
	// A newline is the next line, from column zero. Anything the old line
	// held is gone as far as these tests are concerned.
	case NEWLINE:
	    term_line++;
	    term_pos = 0;
	    term_line_size = 0;
	    memset(term_buf, 0, sizeof(term_buf));
	    break;
	// Column zero of the SAME line -- the cursor moves, the text stays.
	case CARRIAGE_RETURN:
	    term_pos = 0;
	    break;
	default:
	    if (term_pos < TERMBUF_SIZE-1) {
		term_buf[term_pos++] = c;
		if (term_pos > term_line_size)
		    term_line_size = term_pos;
	    }
	    break;
	}
	break;
    case 1:
	// 'O' as well as '[': some terminals send ESC O A for the arrow keys in
	// application cursor mode. The editor emits neither -- this arm exists
	// so a sequence added later is decoded rather than typed.
	if ((c == '[') || (c == 'O')) {
	    term_esc = 2;
	    term_arg = -1;
	}
	else
	    term_esc = 0;
	break;
    case 2:
	// && , not ||. With || every byte is "a digit" -- 'A' is >= '0' -- so
	// the final byte was eaten as a parameter, the sequence never ended and
	// the decoder stayed in state 2 for the rest of the run.
	if ((c >= '0') && (c <= '9')) {
	    if (term_arg == -1)
		term_arg =  (c - '0');
	    else
		term_arg = term_arg*10 + (c - '0');
	    return 1;
	}
	switch(c) {
	case 'A': term_line -= ((term_arg==-1) ? 1 : term_arg); break;
	case 'B': term_line += ((term_arg==-1) ? 1 : term_arg); break;
	case 'C': term_pos  +=  ((term_arg==-1) ? 1 : term_arg); break;
	case 'D': term_pos  -=  ((term_arg==-1) ? 1 : term_arg); break;
	default: break;
	}
	if (term_pos < 0) term_pos = 0;
	// The cursor may rest one past the last character -- that is where the
	// next one goes. Clamping to size-1 put it on top of the last cell, so
	// the next character printed overwrote it.
	if (term_pos > term_line_size) term_pos = term_line_size;
	term_esc = 0;
	break;
    }
    return 1;
}

int csp_print_str(const char* s)
{
    int n = 0;
    while (s[n]) {
	csp_print_char(s[n]);
	n++;
    }
    return n;
}
int csp_print_rostr(rostring_t s)
{
    return csp_print_str((const char*)s);
}

int csp_println(void)
{
    return csp_print_char(NEWLINE);
}

int csp_print_uint(uvalue_t v)
{
    char b[10];
    int n = 0, i;
    do {
	b[n++] = (char)('0' + (v % 10));
	v /= 10;
    } while (v);
    for (i = n; i > 0; i--)
	csp_print_char(b[i-1]);
    return n;
}

void csp_flush(void)
{
}

static int fails = 0;

// Two answers to the same question, and both have to agree.
//
// `got` is the BUFFER -- what the parser will be handed. `term` is the SCREEN --
// what the person typing is looking at. They are produced by entirely separate
// code: the buffer by the edit, the screen by the backspaces and spaces the
// editor printed to describe that edit. An editor that got one right and the
// other wrong would be the worst kind: correct output from a line that reads as
// something else, or a line that reads right and parses as something else.
//
// A TERM mismatch FAILS. It used to only print, which meant the screen half
// asserted nothing at all and could rot without anyone noticing.
static void ck(const char* what, const char* want, const char* got,
	       const char* term)
{
    if (strcmp(want, got) != 0) {
	printf("  FAIL %s: buffer: want \"%s\", got \"%s\"\n", what, want, got);
	fails++;
    }
    else if (strcmp(want, term) != 0) {
	printf("  FAIL %s: screen: want \"%s\", got \"%s\" [pos=%d,size=%d]\n",
	       what, want, term, term_pos, term_line_size);
	fails++;
    }
    else {
	printf("  PASS %s\n", what);
    }
}

// The cursor, in both places again: st.cur is where the next character goes in
// the BUFFER, term_pos is the column it will appear in. They differ by the width
// of the prompt and by nothing else -- if they drift apart, the next character
// typed lands in one place and is drawn in another.
static void ck_cur(const char* what, int want)
{
    if ((int)st.cur != want) {
	printf("  FAIL %s: buffer cursor: want %d, got %d\n",
	       what, want, (int)st.cur);
	fails++;
    }
    else if (term_pos != want + 2) {           // 2 == strlen("> ")
	printf("  FAIL %s: screen column: want %d, got %d\n",
	       what, want + 2, term_pos);
	fails++;
    }
    else {
	printf("  PASS %s\n", what);
    }
}

static void ck_int(const char* what, int want, int got)
{
    if (want == got) {
	printf("  PASS %s\n", what);
    } else {
	printf("  FAIL %s: want %d, got %d\n", what, want, got);
	fails++;
    }
}

static void reset(void)
{
    memset(&st, 0, sizeof(st));
    st.buf = linebuf;
    st.buf_size = (uint16_t)sizeof(linebuf);
    st.hist = histbuf;
    st.hist_size = (uint16_t)sizeof(histbuf);
    csp_line_init(&st);

    memset(term_buf, 0, sizeof(term_buf));
    term_pos  = 0;
    term_line = 0;
    term_arg  = -1;
    term_esc  = 0;
    term_line_size = 0;
}

static void feed(const char* s)
{
    while (*s)
	csp_line_input(&st, *s++);
}

// A completed line, as the REPL would take it -- then cleared the way
// csp_line_done does, so the next line starts fresh.
static void enter(void)
{
    csp_line_input(&st, NEWLINE);
    st.pos = 0;
    st.cur = 0;
    st.fill = 0;
    st.ready = 0;
}

// What is in the line right now.
static const char* line(void)
{
    static char out[130];
    memcpy(out, st.buf, st.pos);
    out[st.pos] = '\0';
    return out;
}

// What a reader would say is on the screen right now.
//
// Trailing spaces come off. They are not text -- they are how the editor rubs
// out what the line used to be, and a terminal has no other way to do it: when
// a 23-character recall is replaced by a 2-character one, the 21 cells that are
// no longer part of the line still hold whatever was printed over them, and what
// is printed over them is spaces. The line ENDS at the last non-space, and that
// is the thing worth asserting about.
//
// The cost is that a line ending in a space one meant to type reads as if it did
// not. No test types one, and if one ever needs to, compare term_buf directly.
static const char* term(void)
{
    static char out[TERMBUF_SIZE];
    int n = (term_line_size < TERMBUF_SIZE) ? term_line_size : TERMBUF_SIZE-1;
    int i = 0;

    // The prompt is printed by the editor and is not part of the line. Decided
    // BEFORE the trim: an empty line is a bare "> ", and trimming first eats
    // the space that identifies it, leaving the '>' to be read as text.
    if ((n >= 2) && (term_buf[0] == '>') && (term_buf[1] == SPACE))
	i = 2;
    while ((n > i) && (term_buf[n-1] == SPACE))
	n--;
    memcpy(out, term_buf + i, (size_t)(n - i));
    out[n - i] = '\0';
    return out;
}


int main(void)
{
    printf("line editor:\n");

    // --- typing and backspace, which is all the old editor could do ---------
    reset();
    feed("abc");
    ck("plain typing", "abc", line(), term());
    feed("\b");
    ck("backspace at the end", "ab", line(), term());
    feed("de");
    feed("\b\b");
    ck("backspace at the end", "ab", line(), term());
    feed("\b\b");
    feed("ba");
    ck("backspace at the end", "ba", line(), term());    
    

    // --- the cursor --------------------------------------------------------
    //
    // The case that motivated the whole thing: recall a declaration and change
    // a character in the MIDDLE, not just at the end.
    reset();
    feed("Dout1");
    feed("\002\002");                   // Ctrl-B twice: between 'u' and 't'
    ck_cur("cursor moved left", 3);
    feed("X");
    ck("insert in the middle", "DouXt1", line(), term());
    ck_cur("cursor follows the insert", 4);

    feed("\006");                       // Ctrl-F
    feed("\b");                         // delete what it just passed
    ck("backspace deletes left of the cursor", "DouX1", line(), term());

    feed("\001");                       // Ctrl-A
    ck_cur("home", 0);
    feed("#");
    ck("insert at the start", "#DouX1", line(), term());

    feed("\005");                       // Ctrl-E
    ck_cur("end", 6);

    // Ctrl-K from the middle keeps the head and drops the tail.
    feed("\001\006\006\006");           // home, then three right
    feed("\013");                       // Ctrl-K
    ck("kill to end of line", "#Do", line(), term());

    // Ctrl-U throws the WHOLE line away, wherever the cursor happens to be --
    // not the head, the way readline's unix-line-discard would. A serial console
    // in raw mode has no other way to start over, and starting over is what one
    // reaches for it to do.
    //
    // Worth its own case for a duller reason: it was the one binding nothing
    // covered, and it was missing its `break`. The fall-through landed on Ctrl-K,
    // which found an empty line and did nothing -- so the bug was invisible and
    // would have stayed that way until a key was added between the two.
    feed("abc\002");                    // three characters, cursor one from end
    feed("\025");                       // Ctrl-U
    ck("kill the whole line", "", line(), term());
    ck_cur("and the cursor comes home", 0);
    feed("x");
    ck("and typing starts over", "x", line(), term());

    // --- history -----------------------------------------------------------
    reset();
    feed("#digital Dout1 out 1:22"); enter();
    feed("#digital Dout2 out 1:23"); enter();

    feed("\020");                       // Ctrl-P: the newest
    ck("recall the last line", "#digital Dout2 out 1:23", line(), term());
    feed("\020");                       // again: the one before
    ck("recall the one before", "#digital Dout1 out 1:22", line(), term());
    feed("\016");                       // Ctrl-N: forward again
    ck("forward through history", "#digital Dout2 out 1:23", line(), term());
    feed("\016");                       // past the newest: an empty line
    ck("past the newest clears the line", "", line(), term());

    // Recall then EDIT -- Tony's actual case: same line, next port.
    reset();
    feed("#digital Dout1 out 1:22"); enter();
    feed("\020");
    feed("\005");                       // to the end
    feed("\b");                         // 22 -> 2
    feed("3");                          // -> 23
    ck("recalled and edited", "#digital Dout1 out 1:23", line(), term());

    // A shorter recall must not leave the tail of the longer line behind IN
    // THE BUFFER -- the screen is redrawn separately, but line_pos is what the
    // parser sees, and a stale tail would be parsed.
    reset();
    feed("aaaaaaaaaa"); enter();
    feed("bb"); enter();
    feed("\020\020");                   // back to the long one, then...
    feed("\016");                       // ...forward to the short one
    ck("a shorter recall truncates", "bb", line(), term());
    ck_int("and line_pos follows", 2, (int)st.pos);

    // The same line twice running is remembered ONCE: repeating a command is
    // the commonest thing anyone does at a prompt.
    reset();
    feed("/list"); enter();
    feed("/list"); enter();
    feed("\020");
    ck("a repeat is stored once", "/list", line(), term());
    feed("\020");
    ck("and there is nothing older", "/list", line(), term());

    // --- arrow keys --------------------------------------------------------
    reset();
    feed("abc"); enter();
    feed("\033[A");                     // Up
    ck("arrow up recalls", "abc", line(), term());
    feed("\033[D\033[D");               // Left, Left
    ck_cur("arrow left moves the cursor", 1);
    feed("\033[C");                     // Right
    ck_cur("arrow right moves it back", 2);
    feed("Z");
    ck("insert after an arrow", "abZc", line(), term());

    // An unrecognised sequence is SWALLOWED, not typed. A function key used to
    // land in the middle of a command as `[15~`.
    reset();
    feed("ab\033[15~cd");
    ck("an unknown escape sequence is swallowed", "abcd", line(), term());

    // --- the paste queue ---------------------------------------------------
    //
    // While re-feeding, a byte that looks like a cursor key must do NOTHING:
    // the re-feed walks the same buffer it writes into, and it rests on "one
    // byte in advances the write cursor by at most one".
    reset();
    feed("abc"); enter();
    st.refeed = 1;
    feed("\020");
    ck("no recall while re-feeding a paste", "", line(), term());
    feed("\033[A");
    ck("no arrow keys either", "", line(), term());
    st.refeed = 0;

    printf("line editor: %s\n", fails ? "FAILED" : "ok");
    return fails != 0;
}
