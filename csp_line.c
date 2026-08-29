// Line editor
#include "csp_line.h"
#include "csp_print.h"

#define CTRL(x) ((x) - 64)
#define ESCAPE          27
#define DELETE          127
#define SPACE           32
#define BACKSPACE       '\b'
#define NEWLINE         '\n'
#define CARRIAGE_RETURN '\r'
#define BELL            '\a'

// ============================================================
// Line input -- the editable buffer itself, not the commands. csp_mem_init
// carves it off the arena and csp_rt_init initialises it, so it belongs on the
// runtime side: an exec-only build still has a line buffer, and a binary/IAP
// front end would read into the same one.
// ============================================================

void csp_line_init(csp_line_t* st)
{
    st->pos = 0;
    st->cur = 0;
    st->fill = 0;
    st->ready = 0;
    st->ovf = 0;
    st->esc = 0;
    st->refeed = 0;
    st->need_prompt = 1;
    // NOT the history: it survives a /clear and a /load, because what you typed
    // is yours and has nothing to do with what the program now contains.
    st->hist_at = st->hist_used;
}

void csp_line_prompt(csp_line_t* st)
{
    if (st->need_prompt) {
	csp_print_lit("> ");
	csp_flush();
	st->need_prompt = 0;
    }
}

int csp_line_space(csp_line_t* st)
{
    return st->fill < st->buf_size;
}

// Done with the line at the front. Anything that arrived while it ran was stored
// raw behind it; move that down to the start and put it back through the normal
// path, so it is echoed and edited exactly as if it had just come in. That is
// what keeps the echo interleaved with the results during a paste instead of
// showing the whole file and then a wall of OK.
//
// The move is done in place by re-feeding: the write cursor starts at 0 and the
// read cursor at src, and one character advances the write cursor by at most
// one, so the write never overtakes the read. src >= 2 always -- an empty line
// never becomes ready -- which keeps the two apart even on the first byte.
void csp_line_done(csp_line_t* st)
{
    uint16_t src = st->pos + 1;    // past the line and its terminator
    uint16_t n   = (st->fill > src) ? (uint16_t)(st->fill - src) : 0;
    uint16_t k;

    st->pos = 0;
    st->cur = 0;
    st->fill = 0;
    st->ready = 0;
    // Cursor keys and history are OFF for the duration. csp_line_input is being
    // fed out of the very buffer it is writing into, and the whole thing rests
    // on "one byte in advances the write cursor by at most one". A recall puts
    // a whole line in at once, and the write would overtake the read.
    st->refeed = 1;
    for (k = 0; k < n; k++)
	csp_line_input(st, st->buf[src + k]);
    st->refeed = 0;
}

// --- command history ---------------------------------------------------------
//
// Entries are LENGTH-TEXT-LENGTH. The trailing copy is what makes a backwards
// walk possible without an index: from the end, the last byte IS the length of
// the entry that ends there, so its start is len+2 back. The leading copy does
// the same going forwards, which is how the oldest is dropped when the buffer
// fills. Two bytes an entry for both directions and no table.

#if defined(CSP_HIST_SHARE)

static void hist_drop_oldest(csp_line_t* st)
{
    uint16_t first = (uint16_t)((uint8_t)st->hist[0] + 2);

    if (first >= st->hist_used) {
	st->hist_used = 0;
	return;
    }
    memmove(st->hist, st->hist + first, (size_t)(st->hist_used - first));
    st->hist_used = (uint16_t)(st->hist_used - first);
}

static void hist_push(csp_line_t* st, const char* s, uint16_t n)
{
    uint16_t need = (uint16_t)(n + 2);

    if ((st->hist == NULL) || (n == 0) || (n > CSP_HIST_LINE_MAX))
	return;
    if (need > st->hist_size)
	return;
    // Not the same line twice running. Repeating a command is the commonest
    // thing anyone does at a prompt, and remembering it once per press fills
    // the history with one line.
    if (st->hist_used >= need) {
	uint16_t last = (uint16_t)((uint8_t)st->hist[st->hist_used - 1]);
	if (last == n) {
	    uint16_t start = (uint16_t)(st->hist_used - last - 2);
	    if (memcmp(st->hist + start + 1, s, n) == 0)
		return;
	}
    }
    while (((uint16_t)(st->hist_used + need) > st->hist_size) &&
	   (st->hist_used > 0))
	hist_drop_oldest(st);
    if ((uint16_t)(st->hist_used + need) > st->hist_size)
	return;
    st->hist[st->hist_used] = (char)n;
    memcpy(st->hist + st->hist_used + 1, s, n);
    st->hist[st->hist_used + 1 + n] = (char)n;
    st->hist_used = (uint16_t)(st->hist_used + need);
}

#endif /* CSP_HIST_SHARE */

// --- the line editor ---------------------------------------------------------
//
// The cursor is line_cur and the length is line_pos, and keeping them apart is
// what lets a recalled line be corrected in the MIDDLE -- `#digital Dout1 out
// 1:22` into `Dout2 ... 1:23` -- rather than only from the end backwards.
//
// Redrawing is done with the three characters every terminal agrees on: '\b',
// ' ' and the text itself. No escape sequences are emitted, so this works on a
// dumb terminal, a serial monitor and a pipe alike. Only INPUT understands
// escapes, and only enough of them to recognise the arrow keys.

void csp_line_back(int n)             // move the cursor left n columns
{
    while (n-- > 0)
	csp_print_char(BACKSPACE);
}

// Reprint from the cursor to the end, then come back. Used after any edit that
// changes what is to the RIGHT of the cursor.
void csp_line_tail(csp_line_t* st, int pad)
{
    uint16_t i;
    int pad0 = pad;

    for (i = st->cur; i < st->pos; i++)
	csp_print_char(st->buf[i]);
    while (pad-- > 0)
	csp_print_char(' ');            // rub out what the line used to be
    csp_line_back((int)(st->pos - st->cur)+pad0);
}

void csp_line_insert(csp_line_t* st, char c)
{
    uint16_t i;

    if (st->pos >= st->buf_size - 1) {
	st->ovf = 1;
	csp_print_char(BELL);           // audible while typing, not at the end
	return;
    }
    for (i = st->pos; i > st->cur; i--)
	st->buf[i] = st->buf[i - 1];
    st->buf[st->cur] = c;
    st->pos++;
    csp_print_char(c);
    st->cur++;
    st->fill = st->pos;
    if (st->cur < st->pos)
	csp_line_tail(st, 0);
}

void csp_line_erase_left(csp_line_t* st)
{
    uint16_t i;

    if (st->cur == 0) {
	csp_print_char(BELL);
	return;
    }
    for (i = (uint16_t)(st->cur - 1); i + 1 < st->pos; i++)
	st->buf[i] = st->buf[i + 1];
    st->pos--;
    st->cur--;
    st->fill = st->pos;
    csp_print_char(BACKSPACE);
    csp_line_tail(st, 1);           // one column of old text to rub out
}

void csp_line_home(csp_line_t* st)
{
    csp_line_back((int)st->cur);
    st->cur = 0;
}

void csp_line_end(csp_line_t* st)
{
    while (st->cur < st->pos)
	csp_print_char(st->buf[st->cur++]);
}

void csp_line_left(csp_line_t* st)
{
    if (st->cur > 0) {
	csp_print_char(BACKSPACE);
	st->cur--;
    }
}

void csp_line_right(csp_line_t* st)
{
    if (st->cur < st->pos)
	csp_print_char(st->buf[st->cur++]);
}

void csp_line_kill_to_end(csp_line_t* st)
{
    int n = (int)(st->pos - st->cur);

    if (n <= 0)
	return;
    st->pos = st->cur;
    st->fill = st->pos;
    csp_line_tail(st, n);
}

// Put a whole new line in place of the current one. The rub-out count is the
// difference in length, so a shorter line does not leave the tail of the longer
// one behind it on the screen. s may be NULL for "empty".
void csp_line_replace(csp_line_t* st, const char* s, uint16_t n)
{
    int old = (int)st->pos;

    csp_line_home(st);
    if (n > st->buf_size - 1)
	n = (uint16_t)(st->buf_size - 1);
    if (n && s)
	memcpy(st->buf, s, n);
    st->pos = n;
    st->fill = n;
    st->ovf = 0;
    csp_line_tail(st, (old > (int)n) ? (old - (int)n) : 0);
    csp_line_end(st);
}

// Walk the history. dir < 0 is older (Ctrl-P, Up), dir > 0 is newer.
//
// Newer past the end clears the line rather than sticking on the newest entry:
// that is the way back to an empty prompt without reaching for Ctrl-U, and it
// matches what every shell does.
void csp_line_recall(csp_line_t* st, int dir)
{
#if defined(CSP_HIST_SHARE)
    // Silent during a re-feed rather than a bell: a pasted file that happens to
    // carry a control byte should not make the terminal beep once per line.
    if (st->refeed)
	return;
    if ((st->hist == NULL) || (st->hist_used == 0)) {
	csp_print_char(BELL);
	return;
    }
    if (dir < 0) {
	uint16_t len;

	if (st->hist_at == 0) {         // already at the oldest
	    csp_print_char(BELL);
	    return;
	}
	len = (uint16_t)((uint8_t)st->hist[st->hist_at - 1]);
	st->hist_at = (uint16_t)(st->hist_at - len - 2);
	csp_line_replace(st, st->hist + st->hist_at + 1, len);
    }
    else {
	uint16_t len = (uint16_t)((uint8_t)st->hist[st->hist_at]);
	uint16_t next = (uint16_t)(st->hist_at + len + 2);

	if (next >= st->hist_used) {    // past the newest: an empty line
	    st->hist_at = st->hist_used;
	    csp_line_replace(st, NULL, 0);
	    return;
	}
	st->hist_at = next;
	len = (uint16_t)((uint8_t)st->hist[next]);
	csp_line_replace(st, st->hist + next + 1, len);
    }
#else
    (void)st; (void)dir;
    csp_print_char(BELL);
#endif
}

// ESC [ A and friends. Three states and no allocation: ESC, then '[' or 'O'
// (some terminals send the latter in application cursor mode), then the letter.
// A sequence that is not recognised is SWALLOWED rather than typed into the
// line -- a stray function key used to land as `[15~` in the middle of a
// command.
//
// Returns 1 if the byte was part of a sequence and is now dealt with.
int csp_line_escape(csp_line_t* st, char c)
{
    if (st->esc == 0) {
	if (c != ESCAPE)
	    return 0;
	st->esc = 1;
	return 1;
    }
    if (st->esc == 1) {
	st->esc = (uint8_t)(((c == '[') || (c == 'O')) ? 2 : 0);
	return 1;
    }
    // esc == 2: the final byte, or a digit on the way to one (ESC [ 3 ~).
    if ((c >= '0') && (c <= '9'))
	return 1;                       // still coming; stay in state 2
    st->esc = 0;
    // A re-feed DECODES the sequence and then drops it. Skipping the decoder
    // instead is not the same thing: ESC is unprintable and vanishes, but the
    // '[' and the 'A' after it are ordinary characters and get typed into the
    // line. Swallowing has to happen in both modes; only the ACTION is off.
    if (st->refeed)
	return 1;
    switch (c) {
    case 'A': csp_line_recall(st, -1); break;
    case 'B': csp_line_recall(st, +1); break;
    case 'C': csp_line_right(st);      break;
    case 'D': csp_line_left(st);       break;
    case 'H': csp_line_home(st);       break;
    case 'F': csp_line_end(st);        break;
    default: break;                 // ~ and everything else: swallowed
    }
    csp_flush();
    return 1;
}

void csp_line_input(csp_line_t* st, char c)
{
    // A complete line is already waiting to run, so this byte belongs to a LATER
    // line: queue it raw -- no echo, no editing -- and let csp_line_done deal
    // with it. Editing a line you have not seen run yet is not a thing anyone
    // does, and appending it to the ready line is the bug this replaced.
    if (st->ready) {
	if (st->fill < st->buf_size)
	    st->buf[st->fill++] = c;
	return;
    }
    // The prompt belongs immediately before the first character of a line, and
    // this is the only place that knows one is starting. The caller's own
    // csp_line_prompt covers the idle case, but it never runs during a re-feed
    // out of the queue -- that happens inside csp_line_done, several frames
    // below the loop -- so every pasted line after the first echoed bare.
    csp_line_prompt(st);

    // Arrow keys and anything else escape-introduced. Decoded even during a
    // re-feed -- see line_escape for why swallowing and ignoring are not the
    // same thing -- but acted on only when a human is typing.
    if (csp_line_escape(st, c))
	return;

    switch(c) {
    case NEWLINE:
    case CARRIAGE_RETURN:
     // line_ovf: a character had to be dropped because the buffer was full, so
     // the line is REFUSED rather than run short. A truncated command is not a
     // harmless partial, it is a different command -- `#disable 12` cut to
     // `#disable 1` disables the wrong rule and says nothing.
	if (st->ovf) {
	    st->pos = 0;
	    st->fill = 0;
	    st->ovf = 0;
	    csp_println();     // the echoed line has no newline yet -- without this
			       // the complaint lands on the end of the input itself
	    csp_print_lit("Error: line too long, max ");
	    csp_print_uint(st->buf_size - 1);
	    csp_print_line(" characters -- line ignored");
	    csp_flush();
	    // Print the next prompt here and return: no line was made ready, so
	    // the caller never gets back to the point where it would print one,
	    // and the following line would be echoed with nothing in front of it.
	    st->need_prompt = 1;
	    csp_line_prompt(st);
	    return;
	}
	else if (st->pos > 0) {
#if defined(CSP_HIST_SHARE)
	    hist_push(st, st->buf, st->pos);
#endif
	    st->buf[st->pos] = '\0';
	    st->ready = 1;
	    st->fill = st->pos + 1;   // the queue starts after the NUL
	}
	csp_println();
	st->cur = 0;
	st->esc = 0;
#if defined(CSP_HIST_SHARE)
	st->hist_at = st->hist_used;            // browsing starts at the newest
#endif
	st->need_prompt = 1;
	break;
    case BACKSPACE:
    case DELETE: csp_line_erase_left(st); break;
    case CTRL('U'): csp_line_replace(st, NULL, 0); break;
    case CTRL('K'): csp_line_kill_to_end(st); break;
    case CTRL('A'): csp_line_home(st); break;	
    case CTRL('E'): csp_line_end(st); break;
    case CTRL('B'): csp_line_left(st); break;
    case CTRL('F'): csp_line_right(st); break;
    case CTRL('P'): csp_line_recall(st, -1); break;
    case CTRL('N'): csp_line_recall(st, +1); break;
    default:
	if (c >= SPACE && c < DELETE)
	    csp_line_insert(st, c);
	break;
    }
    csp_flush();
}
