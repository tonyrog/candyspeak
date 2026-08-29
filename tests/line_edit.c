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
// port delivers them. What is checked is the BUFFER -- what the line came out
// as -- not the echo, which is the terminal's business.

#include "../csp.h"

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

// insert the expected terminal output
int csp_print_char(char c)
{
    if (isprint(c))
	printf("CHAR '%c' term_pos=%d, term_line_size=%d\n",
	       c, term_pos, term_line_size);
    else
	printf("CHAR '0x%02x' term_pos=%d, term_line_size=%d\n",
	       c, term_pos, term_line_size);
    
    switch(term_esc) {
    case 0:
	switch(c) {
	case BACKSPACE:
	    if (term_pos > 0) {
		term_pos--;
		// if we backspace over a space character at the
		// end of line then decrease line size
		if (term_buf[term_pos] == SPACE) {
		    if (term_line_size > 0)
			term_line_size--;
		}
	    }
	    break;
	case BELL: break;
	case ESCAPE: term_esc = 1; break;
	default:
	    term_buf[term_pos++] = c;
	    if (term_pos > term_line_size) {
		term_line_size = term_pos;
	    }
	    break;
	}
	break;
    case 1:
	if (c == '[') {
	    term_esc = 2;
	    term_arg = -1;
	}
	else
	    term_esc = 0;
	break;
    case 2:
	if ((c >= '0') || (c <= '9')) {
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
	if (term_pos >= term_line_size) term_pos = term_line_size-1;
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
    csp_print_char(NEWLINE);
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

static void ck(const char* what, const char* want, const char* got,
	       const char* term)
{
    if (strcmp(want, term) != 0) {
	printf("  TERM %s: want \"%s\", got \"%s\" [pos=%d,size=%d]\n",
	       what, want, term, term_pos, term_line_size);
    }
    if (strcmp(want, got) == 0) {
	printf("  PASS %s\n", what);
    } else {
	printf("  FAIL %s: want \"%s\", got \"%s\"\n", what, want, got);
	fails++;
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

static const char* term(void)
{
    char* ptr = term_buf;
    // skip prompt
    if ((ptr[0] == '>') && (ptr[1] == ' '))
	ptr += 2;
    // mark complete line
    if (term_line_size < TERMBUF_SIZE)
	term_buf[term_line_size] = '\0';
    else
	term_buf[TERMBUF_SIZE-1] = '\0';
    return ptr;
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
    ck_int("cursor moved left", 3, (int)st.cur);
    feed("X");
    ck("insert in the middle", "DouXt1", line(), term());
    ck_int("cursor follows the insert", 4, (int)st.cur);

    feed("\006");                       // Ctrl-F
    feed("\b");                         // delete what it just passed
    ck("backspace deletes left of the cursor", "DouX1", line(), term());

    feed("\001");                       // Ctrl-A
    ck_int("home", 0, (int)st.cur);
    feed("#");
    ck("insert at the start", "#DouX1", line(), term());

    feed("\005");                       // Ctrl-E
    ck_int("end", 6, (int)st.cur);

    // Ctrl-K from the middle keeps the head and drops the tail.
    feed("\001\006\006\006");           // home, then three right
    feed("\013");                       // Ctrl-K
    ck("kill to end of line", "#Do", line(), term());

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
    ck_int("arrow left moves the cursor", 1, (int)st.cur);
    feed("\033[C");                     // Right
    ck_int("arrow right moves it back", 2, (int)st.cur);
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
