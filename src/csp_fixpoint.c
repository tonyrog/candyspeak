#include "../include/csp_fixpoint.h"

fixpoint_t fix_add(fixpoint_t a, fixpoint_t b)
{
    return a + b;
}

fixpoint_t fix_sub(fixpoint_t a, fixpoint_t b)
{
    return a - b;
}

fixpoint_t fix_neg(fixpoint_t a)
{
    return -a;
}

// Multiplication: (a * b) >> 16
// Use 64-bit intermediate to avoid overflow
fixpoint_t fix_mul(fixpoint_t a, fixpoint_t b)
{
    return ((fixpoint_t)(((int64_t)(a) * (int64_t)(b)) >> FIX_SHIFT));
}

// Division: (a << 16) / b
// Use 64-bit intermediate to avoid overflow
// b == 0 is 0, as the integer division is: a trap is no answer (csp_rt.c).
fixpoint_t fix_div(fixpoint_t a, fixpoint_t b)
{
    if (b == 0)
	return 0;
    return (fixpoint_t)(((int64_t)(a) << FIX_SHIFT) / (b));
}

// Fixed-point to integer, toward zero -- what "truncate" has always claimed to
// mean here. A bare `>> FIX_SHIFT` is an arithmetic shift and therefore FLOOR,
// which sends -2.5 to -3 while a C cast (the float build's conversion) sends it
// to -2. Working from the magnitude makes the two builds agree, and negating
// through uint32_t keeps INT32_MIN out of undefined behaviour.
int32_t fix_trunc(fixpoint_t f)
{
    int neg = (f < 0);
    uint32_t a = neg ? -(uint32_t)f : (uint32_t)f;
    int32_t  r = (int32_t)(a >> FIX_SHIFT);
    return neg ? -r : r;
}

// Fixed-point to integer, nearest, halves rounded AWAY from zero -- what C's
// round() does. Adding half before an arithmetic shift would instead round
// halves toward +infinity, so -2.5 and 2.5 would not be mirror images.
int32_t fix_round(fixpoint_t f)
{
    int neg = (f < 0);
    uint32_t a = neg ? -(uint32_t)f : (uint32_t)f;
    int32_t  r = (int32_t)((a + (FIX_SCALE/2)) >> FIX_SHIFT);
    return neg ? -r : r;
}

// Integer sqrt for fixed-point (result is Q16.16)
fixpoint_t fix_sqrt(fixpoint_t x)
{
    uint32_t val, result, bit;
    
    if (x <= 0) return 0;

    val = (uint32_t)x;
    result = 0;
    bit = 1UL << 30;

    // Find highest bit
    while (bit > val) bit >>= 2;

    while (bit != 0) {
        if (val >= result + bit) {
            val -= result + bit;
            result = (result >> 1) + bit;
        } else {
            result >>= 1;
        }
        bit >>= 2;
    }

    // Shift for Q16.16 (input is Q16.16, sqrt needs adjustment)
    return (fixpoint_t)(result << (FIX_SHIFT / 2));
}

// --- IEEE binary32 bits <-> Q16.16, in integers -----------------------------
//
// An image keeps every float constant as IEEE binary32 bits (ROM format 23):
// one image for every node, whatever its float is. A Q16.16 node turns them
// into its own on the way in (OP_LIFH, a declaration's start value), and the
// compiler -- on any node -- folds a float expression only when the answer is
// EXACT in both formats, which these decide with integers alone: no float
// arithmetic is needed to compile, and none is on an FPU-less part.

// IEEE bits as Q16.16: rounded to the nearest, halves away from zero; beyond
// the range, the end of it. A subnormal is 0.
int32_t fix_from_ieee(uint32_t b)
{
    int e = (int)((b >> 23) & 0xff);
    uint32_t m = (b & 0x7fffff) | 0x800000;
    int sh = e - 134;            // value * 2^16 = m * 2^(e - 127 - 23 + 16)
    uint32_t q;

    if (e == 0)
	return 0;
    if ((e == 0xff) || (sh > 7))
	q = 0x7fffffff;
    else if (sh >= 0)
	q = m << sh;
    else if (sh < -24)
	q = 0;
    else
	q = (m + (1UL << (-sh - 1))) >> -sh;
    return (b >> 31) ? -(int32_t)q : (int32_t)q;
}

// Whether IEEE bits are a value Q16.16 holds exactly.
int fix_ieee_exact(uint32_t b)
{
    int e = (int)((b >> 23) & 0xff);
    uint32_t m = (b & 0x7fffff) | 0x800000;
    int sh = e - 134;

    if ((b & 0x7fffffff) == 0)
	return 1;                // +0, -0
    if ((e == 0) || (e == 0xff) || (sh > 7))
	return 0;
    if (sh >= 0)
	return 1;
    if (sh < -24)
	return 0;
    return (m & ((1UL << -sh) - 1)) == 0;
}

// Q16.16 as IEEE bits, rounded to the nearest even when it has more than 24
// significant bits.
uint32_t fix_to_ieee(int32_t q)
{
    uint32_t s = (q < 0) ? 0x80000000UL : 0;
    uint32_t a = (q < 0) ? -(uint32_t)q : (uint32_t)q;
    int p = 31;
    uint32_t m;

    if (a == 0)
	return 0;
    while (!(a & (1UL << p)))
	p--;
    if (p > 23) {
	int d = p - 23;
	uint32_t half = 1UL << (d - 1), rest = a & ((1UL << d) - 1);
	m = a >> d;
	if ((rest > half) || ((rest == half) && (m & 1)))
	    m++;
	if (m == 0x1000000) {    // rounded up past 24 bits
	    m >>= 1;
	    p++;
	}
    }
    else
	m = a << (23 - p);
    return s | ((uint32_t)(p - 16 + 127) << 23) | (m & 0x7fffff);
}

// whole + numer/denom as IEEE bits, correctly rounded (to nearest, ties to
// even): the binary digits come by long division, 25 of them and whether
// anything is left. What a float literal is, on every node.
uint32_t fix_ieee_decimal(uint32_t whole, uint32_t numer, uint32_t denom)
{
    uint64_t n = (uint64_t)whole * denom + numer;   // value = n / denom
    uint64_t ip = n / denom, rem = n % denom;
    uint32_t m = 0;
    int bits = 0, e;

    if (n == 0)
	return 0;
    if (ip > 0) {
	int p = 63;
	while (!(ip & ((uint64_t)1 << p)))
	    p--;
	e = p;                   // value = 1.xxx * 2^p
	for (; (p >= 0) && (bits < 25); p--, bits++)
	    m = (m << 1) | (uint32_t)((ip >> p) & 1);
	if (p >= 0)              // integer bits not taken
	    rem |= (ip & (((uint64_t)1 << (p + 1)) - 1)) ? 1 : 0;
    }
    else {
	e = 0;
	while (bits == 0) {      // up to the first one
	    rem <<= 1;
	    e--;
	    if (rem >= denom) {
		rem -= denom;
		m = 1;
		bits = 1;
	    }
	}
    }
    while (bits < 25) {
	rem <<= 1;
	m <<= 1;
	if (rem >= denom) {
	    rem -= denom;
	    m |= 1;
	}
	bits++;
    }
    // m: 25 bits, the 25th the rounding bit; rem: anything after it
    {
	uint32_t r = m & 1;
	m >>= 1;
	if (r && (rem || (m & 1)))
	    m++;
	if (m == 0x1000000) {
	    m >>= 1;
	    e++;
	}
    }
    return ((uint32_t)(e + 127) << 23) | (m & 0x7fffff);
}
