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

static csp_rt_t st;
static char linebuf[128];
static char histbuf[256];

// The platform half. The editor writes its echo through these; this test does
// not check the echo -- what a terminal shows is the terminal's business -- so
// they swallow it and the assertions look at the BUFFER instead.
int csp_print_char(char c) { (void)c; return 1; }
int csp_print_str(const char* s) { int n = 0; while (s[n]) n++; return n; }
int csp_print_rostr(rostring_t s) { return csp_print_str((const char*)s); }
void csp_flush(void) { }
uint32_t csp_time_ms(void) { return 0; }
void* csp_set_file_output(void* f) { (void)f; return NULL; }
int csp_will_output(void) { return 0; }
// csp_rt.c reaches these from code this test never runs; they exist so the
// link closes without dragging in the compiler, the tokenizer and a CAN bus.
int csp_can_send(csp_rt_t* s, uint32_t i, const uint8_t* d, uint8_t n)
{ (void)s; (void)i; (void)d; (void)n; return -1; }
int csp_can_recv(csp_rt_t* s, uint32_t* i, uint8_t* d, uint8_t* n)
{ (void)s; (void)i; (void)d; (void)n; return -1; }
int stack_used(void) { return 0; }

static int fails = 0;

static void ck(const char* what, const char* want, const char* got)
{
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
    st.line_buf = linebuf;
    st.line_buf_size = (uint16_t)sizeof(linebuf);
    st.hist = histbuf;
    st.hist_size = (uint16_t)sizeof(histbuf);
    csp_line_init(&st);
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
    csp_line_input(&st, '\n');
    st.line_pos = 0;
    st.line_cur = 0;
    st.line_fill = 0;
    st.line_ready = 0;
}

// What is in the line right now.
static const char* line(void)
{
    static char out[130];
    memcpy(out, st.line_buf, st.line_pos);
    out[st.line_pos] = '\0';
    return out;
}

int main(void)
{
    printf("line editor:\n");

    // --- typing and backspace, which is all the old editor could do ---------
    reset();
    feed("abc");
    ck("plain typing", "abc", line());
    feed("\b");
    ck("backspace at the end", "ab", line());

    // --- the cursor --------------------------------------------------------
    //
    // The case that motivated the whole thing: recall a declaration and change
    // a character in the MIDDLE, not just at the end.
    reset();
    feed("Dout1");
    feed("\002\002");                   // Ctrl-B twice: between 'u' and 't'
    ck_int("cursor moved left", 3, (int)st.line_cur);
    feed("X");
    ck("insert in the middle", "DouXt1", line());
    ck_int("cursor follows the insert", 4, (int)st.line_cur);

    feed("\006");                       // Ctrl-F
    feed("\b");                         // delete what it just passed
    ck("backspace deletes left of the cursor", "DouX1", line());

    feed("\001");                       // Ctrl-A
    ck_int("home", 0, (int)st.line_cur);
    feed("#");
    ck("insert at the start", "#DouX1", line());

    feed("\005");                       // Ctrl-E
    ck_int("end", 6, (int)st.line_cur);

    // Ctrl-K from the middle keeps the head and drops the tail.
    feed("\001\006\006\006");           // home, then three right
    feed("\013");                       // Ctrl-K
    ck("kill to end of line", "#Do", line());

    // --- history -----------------------------------------------------------
    reset();
    feed("#digital Dout1 out 1:22"); enter();
    feed("#digital Dout2 out 1:23"); enter();

    feed("\020");                       // Ctrl-P: the newest
    ck("recall the last line", "#digital Dout2 out 1:23", line());
    feed("\020");                       // again: the one before
    ck("recall the one before", "#digital Dout1 out 1:22", line());
    feed("\016");                       // Ctrl-N: forward again
    ck("forward through history", "#digital Dout2 out 1:23", line());
    feed("\016");                       // past the newest: an empty line
    ck("past the newest clears the line", "", line());

    // Recall then EDIT -- Tony's actual case: same line, next port.
    reset();
    feed("#digital Dout1 out 1:22"); enter();
    feed("\020");
    feed("\005");                       // to the end
    feed("\b");                         // 22 -> 2
    feed("3");                          // -> 23
    ck("recalled and edited", "#digital Dout1 out 1:23", line());

    // A shorter recall must not leave the tail of the longer line behind IN
    // THE BUFFER -- the screen is redrawn separately, but line_pos is what the
    // parser sees, and a stale tail would be parsed.
    reset();
    feed("aaaaaaaaaa"); enter();
    feed("bb"); enter();
    feed("\020\020");                   // back to the long one, then...
    feed("\016");                       // ...forward to the short one
    ck("a shorter recall truncates", "bb", line());
    ck_int("and line_pos follows", 2, (int)st.line_pos);

    // The same line twice running is remembered ONCE: repeating a command is
    // the commonest thing anyone does at a prompt.
    reset();
    feed("/list"); enter();
    feed("/list"); enter();
    feed("\020");
    ck("a repeat is stored once", "/list", line());
    feed("\020");
    ck("and there is nothing older", "/list", line());

    // --- arrow keys --------------------------------------------------------
    reset();
    feed("abc"); enter();
    feed("\033[A");                     // Up
    ck("arrow up recalls", "abc", line());
    feed("\033[D\033[D");               // Left, Left
    ck_int("arrow left moves the cursor", 1, (int)st.line_cur);
    feed("\033[C");                     // Right
    ck_int("arrow right moves it back", 2, (int)st.line_cur);
    feed("Z");
    ck("insert after an arrow", "abZc", line());

    // An unrecognised sequence is SWALLOWED, not typed. A function key used to
    // land in the middle of a command as `[15~`.
    reset();
    feed("ab\033[15~cd");
    ck("an unknown escape sequence is swallowed", "abcd", line());

    // --- the paste queue ---------------------------------------------------
    //
    // While re-feeding, a byte that looks like a cursor key must do NOTHING:
    // the re-feed walks the same buffer it writes into, and it rests on "one
    // byte in advances the write cursor by at most one".
    reset();
    feed("abc"); enter();
    st.refeed = 1;
    feed("\020");
    ck("no recall while re-feeding a paste", "", line());
    feed("\033[A");
    ck("no arrow keys either", "", line());
    st.refeed = 0;

    printf("line editor: %s\n", fails ? "FAILED" : "ok");
    return fails != 0;
}
