#ifndef __CSP_BITS_H__
#define __CSP_BITS_H__

// Bit-field access for the buffer heap: read or write n bits (n <= 32) at bit
// position `pos`, in either bit order.
//
// WHY NOT bitpack.h. That header was written for sequential bit patterns with
// CONSTANT offsets, where always_inline plus an unrolled byte switch lets the
// compiler fold every mask and shift away. Here the arguments come from a view
// at runtime -- `pos` and `len` are data, not literals -- so nothing folds and
// the unrolling is pure duplication. Measured on an AVR exec-only build:
// csp_heap_get + csp_heap_set were 2 698 bytes, nearly the size of the whole
// evaluator (2 932), for what is conceptually a shift and a mask.
//
// Worse, it took FOUR copies: csp_heap_get held a complete little-endian engine
// AND a complete big-endian one, and csp_heap_set held the mirror pair. The
// endianness is a runtime property of the view, so both always compile in.
//
// This version takes one bit at a time and folds the two orders into ONE
// function each, by moving the endianness into the bit index. It is slower --
// n iterations instead of n/8 byte moves -- but a field is read once per rule
// per cycle, and a 16-bit field costs on the order of ten microseconds on a
// 16 MHz AVR. Size is the scarce resource here, not that.
//
// The bit order is not a matter of taste; it is what a CAN frame on the wire
// expects, and it was established by measuring the old implementation rather
// than by reading its macros:
//
//   little  bit b of the stream is  p[b>>3] & (1    << (b&7))
//           value bit k goes to stream bit pos+k       (LSB first)
//   big     bit b of the stream is  p[b>>3] & (0x80 >> (b&7))
//           value bit n-1-k goes to stream bit pos+k   (MSB first)
//
// tests/unit has no reach here (this is below the language), so the equivalence
// with bitpack.h is proven by tmp/bits/cmp.c -- see tests/repl.sh.

#include <stdint.h>

// One bit of the stream, in the given order. `be` is a plain flag rather than
// two functions so the caller's endianness test stays out of the loop body.
static uint8_t csp_bit_mask(uint16_t b, int be)
{
    return be ? (uint8_t)(0x80u >> (b & 7)) : (uint8_t)(1u << (b & 7));
}

// Read n bits at `pos` into *out, zero-extended.
static void csp_bits_get(const uint8_t* p, uint32_t* out,
			 uint16_t pos, uint8_t n, int be)
{
    uint32_t v = 0;
    uint8_t k;

    for (k = 0; k < n; k++) {
	uint16_t b = pos + k;
	if (p[b >> 3] & csp_bit_mask(b, be))
	    v |= ((uint32_t)1) << (be ? (uint8_t)(n - 1 - k) : k);
    }
    *out = v;
}

// Write the low n bits of v at `pos`, leaving the surrounding bits alone.
static void csp_bits_set(uint8_t* p, uint32_t v,
			 uint16_t pos, uint8_t n, int be)
{
    uint8_t k;

    for (k = 0; k < n; k++) {
	uint16_t b = pos + k;
	uint8_t  m = csp_bit_mask(b, be);
	if ((v >> (be ? (uint8_t)(n - 1 - k) : k)) & 1)
	    p[b >> 3] |= m;
	else
	    p[b >> 3] &= (uint8_t)~m;
    }
}

#endif
