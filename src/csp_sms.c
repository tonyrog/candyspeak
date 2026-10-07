// SMS (TR_SMS) -- the runtime half, the same on every board. A port provides
// only the modem: csp_sms_modem_recv/send/poll (weak defaults in
// csp_transport.c).
//
// Its own file, not csp_transport.c: that one is weak stubs and nothing else,
// so a test can link it without the runtime. This needs the runtime -- the
// #param holding the numbers, the clock, the console.

#include <stdint.h>
#include <stddef.h>
#include "csp.h"
#include "csp_print.h"

//
// A modem's text messages, as a stream (TR_SMS). The runtime half is here and
// is the same on every board; a port provides only csp_sms_modem_recv/send.
//
//   IN    a message from a number on the list becomes its first line plus a
//         newline. From anyone else it is dropped without an answer -- a
//         stranger is not even told the number is a node -- and counted.
//
//   OUT   bytes are gathered into messages. Routed from the interpreter, the
//         console's context markers (CON_MARK in csp.h) say whose they are:
//
//           SMS    the answer to a command that came in a message -- to that
//                  number only, and nothing at all when it printed nothing
//           CYCLE  printed by a rule -- an event, to every number on the list
//           LOCAL  the console's own: the prompt, the echo, a command typed
//                  at it. Dropped.
//
//         Unmarked bytes -- a rule writing the buffer, a route from somewhere
//         that is not the interpreter -- are events.
//
//   CAP   at most CSP_SMS_PER_HOUR messages go out an hour, counted per
//         recipient, and the rest are dropped and counted. A rule that prints
//         every cycle must not be able to empty the SIM; the Alarm module's
//         Repeat is the polite limit, this is the hard one.
//
// ONE MODEM: the state is file-level, so a second `sms` buffer shares it.

#ifndef CSP_SMS_TEXT
#define CSP_SMS_TEXT     161      // 160 characters and a NUL
#endif
#ifndef CSP_SMS_NUM
#define CSP_SMS_NUM       20      // E.164 is at most 15 digits and a '+'
#endif
#ifndef CSP_SMS_PER_HOUR
#define CSP_SMS_PER_HOUR  20
#endif
#define SMS_PEERS          4      // commands in flight, oldest answered first

static char     sms_in[CSP_SMS_TEXT + 1];       // one message, as a line
static uint16_t sms_in_len, sms_in_pos;
static char     sms_peer[SMS_PEERS][CSP_SMS_NUM];
static uint8_t  sms_peer_head, sms_peer_n;
static char     sms_evt[CSP_SMS_TEXT];          // an event being gathered
static uint16_t sms_evt_len;
static uint8_t  sms_evt_fresh;                  // bytes came this cycle
static char     sms_rep[CSP_SMS_TEXT];          // an answer being gathered
static uint16_t sms_rep_len;
static char     sms_rep_to[CSP_SMS_NUM];
static uint8_t  sms_ctx = CON_CTX_CYCLE;        // unmarked bytes are events
static uint32_t sms_hour_t0;
static uint8_t  sms_hour_n;
static uint32_t sms_n_refused, sms_n_capped;

uint32_t csp_sms_refused(void) { return sms_n_refused; }
uint32_t csp_sms_capped(void)  { return sms_n_capped; }

// The digits of a number, so "+46 70-123 45 67" and "+46701234567" are the
// same phone. A leading 00 is the international prefix spelled the other way.
static uint8_t sms_digits(const char* s, uint8_t n, char* d, uint8_t dmax)
{
    uint8_t k = 0, i;
    for (i = 0; (i < n) && s[i]; i++)
	if ((s[i] >= '0') && (s[i] <= '9') && (k < dmax))
	    d[k++] = s[i];
    if ((k >= 2) && (d[0] == '0') && (d[1] == '0')) {
	for (i = 2; i < k; i++)
	    d[i - 2] = d[i];
	k -= 2;
    }
    return k;
}

// The list: the string #param the buffer names, numbers separated by spaces or
// commas. Walked in place -- it is short, and a copy would need a buffer the
// size of the longest list anyone might write.
//
// `fn` is called for each number on it; `want` is a number to look for
// instead (returns 1 if it is there).
static int sms_owners(csp_rt_t* st, uint32_t xref, const char* want,
		      void (*fn)(const char* to, const char* text),
		      const char* text)
{
    value_t v = csp_value(st, (index_t)xref);
    sindex_t h = (sindex_t)v.i;
    uint8_t len = csp_str_len(st, h);
    char num[CSP_SMS_NUM];
    char wd[CSP_SMS_NUM], nd[CSP_SMS_NUM];
    uint8_t wn = 0, nn, i = 0, k;

    if (want)
	wn = sms_digits(want, CSP_SMS_NUM, wd, sizeof(wd));
    while (i < len) {
	char c;
	k = 0;
	while ((i < len) && (((c = (char)csp_str_char(st, h, i)) == ' ') ||
			     (c == ',')))
	    i++;
	while ((i < len) && ((c = (char)csp_str_char(st, h, i)) != ' ') &&
	       (c != ',')) {
	    if (k < sizeof(num) - 1)
		num[k++] = c;
	    i++;
	}
	if (k == 0)
	    continue;
	num[k] = '\0';
	if (want) {
	    nn = sms_digits(num, k, nd, sizeof(nd));
	    if ((nn == wn) && (nn > 0)) {
		for (k = 0; (k < nn) && (nd[k] == wd[k]); k++)
		    ;
		if (k == nn)
		    return 1;
	    }
	}
	else if (fn)
	    fn(num, text);
    }
    return 0;
}

static void sms_out(const char* to, const char* text)
{
    uint32_t now = csp_time_ms();

    if ((uint32_t)(now - sms_hour_t0) >= 3600000UL) {
	sms_hour_t0 = now;
	sms_hour_n = 0;
    }
    if (sms_hour_n >= CSP_SMS_PER_HOUR) {
	sms_n_capped++;
	csp_print_lit("sms! hourly cap, not sent to ");
	csp_print_str(to);
	csp_println();
	return;
    }
    sms_hour_n++;
    // SHOWN on the console, every message, as it is handed to the modem: on
    // a board the only other evidence is a phone that did or did not buzz.
    // Printed outside the cycle and outside an answer, so in the console's
    // own context -- the sink drops it, it never loops back out as an SMS.
    csp_print_lit("sms> ");
    csp_print_str(to);
    csp_print_lit(": ");
    csp_print_str(text);
    csp_println();
    if (csp_sms_modem_send(to, text) < 0) {
	csp_print_lit("sms! modem queue full, not sent to ");
	csp_print_str(to);
	csp_println();
    }
}

// A message's text: the trailing newlines go, the rest is what was printed.
static int sms_close(char* buf, uint16_t* len)
{
    while ((*len > 0) && ((buf[*len - 1] == '\n') || (buf[*len - 1] == '\r')))
	(*len)--;
    buf[*len] = '\0';
    return *len > 0;
}

static void sms_flush_event(csp_rt_t* st, uint32_t xref)
{
    if (sms_close(sms_evt, &sms_evt_len))
	(void)sms_owners(st, xref, NULL, sms_out, sms_evt);
    sms_evt_len = 0;
}

// An answer with nothing in it is not sent: a command whose effect speaks for
// itself (see process_sms_line) leaves it empty on purpose.
static void sms_flush_reply(void)
{
    if (sms_close(sms_rep, &sms_rep_len) && sms_rep_to[0])
	sms_out(sms_rep_to, sms_rep);
    sms_rep_len = 0;
}

// Every cycle: take a message off the modem when the last one has been
// delivered, and send an event that has gone quiet. "Quiet" is a cycle with no
// new bytes -- a rule printing three lines in one cycle is one message.
void csp_sms_tick(csp_rt_t* st, uint32_t xref)
{
    char from[CSP_SMS_NUM];
    char text[CSP_SMS_TEXT];
    uint16_t i;

    if ((sms_evt_len > 0) && !sms_evt_fresh)
	sms_flush_event(st, xref);
    sms_evt_fresh = 0;
    csp_sms_modem_poll();

    if (sms_in_pos < sms_in_len)
	return;                                // still delivering the last one
    from[0] = text[0] = '\0';
    if (csp_sms_modem_recv(from, sizeof(from), text, sizeof(text)) != 1)
	return;
    from[sizeof(from) - 1] = '\0';
    text[sizeof(text) - 1] = '\0';
    if (!sms_owners(st, xref, from, NULL, NULL)) {
	sms_n_refused++;
	return;
    }
    // The FIRST LINE, as a command. A message is typed on a phone; whatever
    // follows a newline in it is not a second command but a signature or a
    // slip of the thumb.
    for (i = 0; text[i] && (text[i] != '\n') && (text[i] != '\r'); i++)
	sms_in[i] = text[i];
    sms_in[i++] = '\n';
    sms_in_len = i;
    sms_in_pos = 0;
    // Who to answer, in the order the commands will run.
    {
	uint8_t slot = (uint8_t)((sms_peer_head + sms_peer_n) % SMS_PEERS);
	if (sms_peer_n == SMS_PEERS) {                 // full: oldest goes
	    sms_peer_head = (uint8_t)((sms_peer_head + 1) % SMS_PEERS);
	    sms_peer_n--;
	}
	for (i = 0; (i < CSP_SMS_NUM - 1) && from[i]; i++)
	    sms_peer[slot][i] = from[i];
	sms_peer[slot][i] = '\0';
	sms_peer_n++;
    }
}

// The line, as a stream. A reader with less room than the line gets what fits
// and a newline -- the rest of a command cut in two would be a second command.
int csp_sms_recv(csp_rt_t* st, uint32_t xref, uint8_t* data, uint16_t* len)
{
    uint16_t n = (uint16_t)(sms_in_len - sms_in_pos);
    uint16_t i;
    (void)st; (void)xref;

    if ((n == 0) || (*len == 0))
	return 0;
    if (n > *len) {
	for (i = 0; i + 1 < *len; i++)
	    data[i] = (uint8_t)sms_in[sms_in_pos + i];
	data[i++] = '\n';
	*len = i;
	sms_in_pos = sms_in_len;
	return 1;
    }
    for (i = 0; i < n; i++)
	data[i] = (uint8_t)sms_in[sms_in_pos + i];
    *len = n;
    sms_in_pos = sms_in_len;
    return 1;
}

int csp_sms_send(csp_rt_t* st, uint32_t xref, const uint8_t* data,
		 uint16_t len)
{
    uint16_t i;

    for (i = 0; i < len; i++) {
	uint8_t c = data[i];

	// NO CARRIAGE RETURNS. A board's println ends lines with \r\n, and in
	// the modem's text mode a \r inside the text ENDS AN INPUT LINE: the
	// modem prompts for more and the message is not what was printed, or
	// does not go at all. A one-line answer got away with it (the trailing
	// \r\n is trimmed); a three-line status report did not. \n is the
	// line break a phone shows.
	if (c == '\r')
	    continue;
	if (CON_IS_MARK(c)) {
	    uint8_t ctx = (uint8_t)(c - CON_MARK(0));
	    if ((sms_ctx == CON_CTX_SMS) && (ctx != CON_CTX_SMS))
		sms_flush_reply();
	    if ((ctx == CON_CTX_SMS) && (sms_ctx != CON_CTX_SMS)) {
		uint8_t k;
		// An event gathered before the answer goes first: it was
		// printed first.
		if (sms_evt_len > 0)
		    sms_flush_event(st, xref);
		sms_rep_len = 0;
		sms_rep_to[0] = '\0';
		if (sms_peer_n > 0) {
		    for (k = 0; k < CSP_SMS_NUM; k++)
			sms_rep_to[k] = sms_peer[sms_peer_head][k];
		    sms_peer_head = (uint8_t)((sms_peer_head + 1) % SMS_PEERS);
		    sms_peer_n--;
		}
	    }
	    sms_ctx = ctx;
	    continue;
	}
	switch (sms_ctx) {
	case CON_CTX_SMS:
	    if (sms_rep_len >= CSP_SMS_TEXT - 1)
		sms_flush_reply();                     // a long answer: in parts
	    sms_rep[sms_rep_len++] = (char)c;
	    break;
	case CON_CTX_CYCLE:
	    if (sms_evt_len >= CSP_SMS_TEXT - 1)
		sms_flush_event(st, xref);
	    sms_evt[sms_evt_len++] = (char)c;
	    sms_evt_fresh = 1;
	    break;
	default:
	    break;                                     // the console's own
	}
    }
    return 0;
}
