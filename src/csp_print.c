/*
 * output function, string,  expr etc
 */

#include "csp_print.h"
#include "csp_strings.h"


// --- numbers and columns ---------------------------------------------------
// Numbers are formatted HERE, on top of csp_print_char, rather than once per
// platform. Two reasons.
//
// The length is exact whether or not output is enabled. A caller padding a
// column needs the width either way, and the per-platform versions returned a
// hard-coded 1 when output was off -- so /state silently lost its alignment
// whenever printing was suppressed.
//
// And the two platforms cannot drift. Serial.print(v, HEX) renders uppercase
// where the host's "0x%x" renders lowercase; that difference was invisible
// until you diffed a board against a host run.
NOINLINE int csp_print_uint(uvalue_t v)
{
    char b[10];                    // 2^32-1 is 10 digits
    int n = 0, i;
    do {
	b[n++] = (char)('0' + (v % 10));
	v /= 10;
    } while (v);
    for (i = n; i > 0; i--)
	csp_print_char(b[i-1]);
    return n;
}

int csp_print_int(ivalue_t v)
{
    if (v < 0) {
	csp_print_char('-');
	// via uvalue_t so the most negative value negates without overflowing
	return 1 + csp_print_uint((uvalue_t)0 - (uvalue_t)v);
    }
    return csp_print_uint((uvalue_t)v);
}

static rochar hex_digits[] RODATA = "0123456789abcdef";

NOINLINE static char hex_digit(uint8_t v)
{
    return (char)ro_byte((rochar*)hex_digits + (v & 0xf));
}

NOINLINE int csp_print_hex(uvalue_t v)
{
    char b[8];
    int n = 0, i;
    csp_print_lit("0x");
    do {
	b[n++] = hex_digit((uint8_t)v);
	v >>= 4;
    } while (v);
    for (i = n; i > 0; i--)
	csp_print_char(b[i-1]);
    return n + 2;
}

// One byte as exactly two digits, no 0x. For dumping raw bytes (a #buffer's
// frame) where the columns have to line up and a leading 0 must not vanish.
int csp_print_hex2(uint8_t v)
{
    csp_print_char(hex_digit(v >> 4));
    csp_print_char(hex_digit(v));
    return 2;
}

// An IPv4 address, dotted quad, from the 32-bit value the scanner made of one.
// The inverse of the literal: `1.2.3.4` goes in as 0x01020304 and comes back
// out as 1.2.3.4.
int csp_print_ipv4(uint32_t a)
{
    int n = 0;
    int i;

    for (i = 24; i >= 0; i -= 8) {
	if (i != 24)
	    n += csp_print_char('.');
	n += csp_print_uint((a >> i) & 0xff);
    }
    return n;
}

// Print s padded to `w` columns. Returns the number of characters written.
//
// A string LONGER than w is never truncated -- the column widens instead. That
// keeps a long name readable at the cost of one ragged row; losing characters
// silently is the worse failure for something you are reading to debug.
//
// LJUST needs no strlen: csp_print_str already returns what it wrote. RJUST and
// CJUST have to measure first, so they pay for it.
static int just_pad(int n)          // n spaces, n <= 0 prints nothing
{
    int i;
    for (i = 0; i < n; i++)
	csp_print_blank();
    return (n > 0) ? n : 0;
}

// How much padding goes before the text for a given justification.
static int just_lead(just_t j, int len, int w)
{
    int lead = (w > len) ? w - len : 0;
    switch (j) {
    case RJUST: return lead;
    case CJUST: return lead / 2;
    default:    return 0;           // LJUST, NJUST
    }
}

int csp_print_just(const char* s, just_t j, int w)
{
    int len, lead;

    // NULL is PADDING, not an empty string to walk. It used to be rewritten to
    // "" and then measured, which meant the two callers that wanted w blanks
    // passed a literal for the function to find the end of -- and a literal in
    // core code is exactly the thing that is in a different address space on a
    // target. csp_print_rojust below has always done it this way.
    if (s == NULL)
	return (j == NJUST) ? 0 : just_pad(w);
    if (j == NJUST)
	return csp_print_str(s);
    if (j == LJUST) {               // no strlen: csp_print_str reports what it wrote
	len = csp_print_str(s);
	return len + just_pad(w - len);
    }
    for (len = 0; s[len]; len++)
	;
    lead = just_lead(j, len, w);
    just_pad(lead);
    csp_print_str(s);
    return lead + len + just_pad(w - len - lead);
}

// Same, for a string in FLASH. Both the print and the length walk go through
// ro_byte -- handing one of these to csp_print_just reads the wrong address
// space on AVR, and the const char* parameter hides that from the compiler.
int csp_print_rojust(rostring_t s, just_t j, int w)
{
    int len, lead;

    if (s == NULL)
	return (j == NJUST) ? 0 : just_pad(w);
    if (j == NJUST)
	return csp_print_rostr(s);
    if (j == LJUST) {
	len = csp_print_rostr(s);
	return len + just_pad(w - len);
    }
    len = ro_strlen(s);
    lead = just_lead(j, len, w);
    just_pad(lead);
    csp_print_rostr(s);
    return lead + len + just_pad(w - len - lead);
}

#if !FVALUE_IS_FIXPOINT
// Fixed-point rendering of a non-negative float below 1e9: integer part, '.',
// six decimals, rounded half-up -- the shape "%f" produces. Split out so the
// scientific branch below can reuse it for its mantissa.
static int print_fixed6(fvalue_t v)
{
    uint32_t ip = (uint32_t)v;
    uint32_t fp = (uint32_t)((v - (fvalue_t)ip) * (fvalue_t)1000000 + (fvalue_t)0.5);
    int n;

    if (fp >= 1000000) {           // the rounding carried into the integer part
	fp -= 1000000;
	ip++;
    }
    n = csp_print_uint(ip);
    csp_print_char('.');
    return n + 1 + csp_print_uintw(fp, 100000);
}
#endif

// Floats are formatted here for the same reason integers are: the host printed
// "%f" (six decimals) while Serial.print(v) printed two, so a board and a host
// disagreed on the same value. And avr-libc's printf has no %f at all, which is
// why the board could not simply use the host's route.
//
// USE_FIXPOINT is not set by any Makefile, so this -- not csp_print_fixpoint --
// is the live path.
int csp_print_float(fvalue_t v)
{
#if FVALUE_IS_FIXPOINT
    return csp_print_fixpoint(v);
#else
    int n = 0;

    if (v != v) {                            // NaN is the only value != itself
	csp_print_lit("nan");
	return 3;
    }
    if (v < (fvalue_t)0) {
	csp_print_char('-');
	v = -v;
	n = 1;
    }
    if (v * (fvalue_t)0 != (fvalue_t)0) {    // finite*0 is 0; inf*0 is NaN
	csp_print_lit("inf");
	return n + 3;
    }
    if (v >= (fvalue_t)1e9) {
	// Too large for a uint32 integer part -- and past 2^24 a float cannot
	// represent integers exactly anyway, so six decimals would be theatre.
	// Print it as d.dddddd e+NN rather than a clamped, wrong number.
	int e = 0;
	while (v >= (fvalue_t)10) { v /= (fvalue_t)10; e++; }
	n += print_fixed6(v);
	csp_print_char('e');
	csp_print_char('+');
	return n + 2 + csp_print_uint((uvalue_t)e);
    }
    return n + print_fixed6(v);
#endif
}

// End of line. Shared, and the ONLY place the runtime should spell a newline
// out -- csp_print_char('\n') says "write this byte" where the caller means
// "end this line", and the two used to drift (Serial.println() wrote "\r\n"
// while a bare '\n' wrote LF, so a board mixed both conventions).
//
// What reaches the wire is the platform's business: csp_print_char prepends the
// CR on Arduino and passes '\n' through on the host. Returns 1 either way --
// logical characters, so column arithmetic matches across platforms.
int csp_println(void)
{
    return csp_print_char('\n');
}

int csp_print_blank(void)
{
    return csp_print_char(' ');
}
