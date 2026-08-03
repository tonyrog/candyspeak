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

// THE BYTE-ALIGNED FAST PATH. A field that starts on a byte boundary and is a
// whole number of bytes needs no bit work at all -- it is a byte move, and the
// bit order decides only which direction. That case is common (a CAN signal is
// usually laid out on byte boundaries, and every plain #variable auto-buffer
// is), and on an 8-bit part the difference is 16 loop iterations against two
// byte moves for a 16-bit field.
//
// The test lives HERE and not as a flag in csp_view_t: `((pos | n) & 7) == 0`
// is an or, an andi and a branch, which is cheaper than computing a flag bit
// in the three setup_* functions that build bit views -- and it means
// csp_can_input/output get the same treatment for free.
//
// That the byte order below is right FOLLOWS from the bit order documented
// above, it is not a separate claim. With pos = 8k:
//
//   little  value bit j -> stream bit pos+j, stream bit b is p[b>>3]&(1<<(b&7))
//           so value bits 0..7 land in p[k]: p[k] is the LEAST significant byte
//   big     value bit n-1-j -> stream bit pos+j, bit b is p[b>>3]&(0x80>>(b&7))
//           so p[k] holds value bits n-1..n-8: p[k] is the MOST significant byte
//
// Both branches move bytes through a uint32_t, which assumes a little-endian
// HOST (AVR, Cortex-M, ESP32, x86 -- every target here). That assumption is not
// left to a comment: tests/bits_cmp.c checks this path against bitpack.h, whose
// shift-based implementation is host-endian-neutral, so a big-endian host fails
// the test loudly. That test also counts how many aligned cases it covered and
// fails if the answer is zero, so the fast path cannot quietly stop being run.

#include <stdint.h>
#include <string.h>

// True when the field is whole bytes on a byte boundary AND fits the 32-bit
// container. The width bound is not decoration: csp_view_t.len is 6 bits, so n
// reaches 64 -- setup_buffer caps a wide view's len at VIEW_MAX (63) and n is
// then 64, which IS byte aligned. Without the bound that lands as a memcpy of
// eight bytes into a uint32_t. The bit loop merely returned nonsense for n > 32
// (a shift past the container); a byte move would corrupt the caller's stack.
#define CSP_BITS_ALIGNED(pos, n)  (((((pos) | (n)) & 7) == 0) && ((n) <= 32))

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

    if (CSP_BITS_ALIGNED(pos, n)) {
	const uint8_t* q = p + (pos >> 3);
	uint8_t nb = (uint8_t)(n >> 3);
	if (!be)
	    memcpy(&v, q, nb);              // p[k] is the low byte: straight copy
	else {
	    uint8_t* d = (uint8_t*)&v;      // p[k] is the high byte: reverse
	    for (k = 0; k < nb; k++)
		d[k] = q[nb - 1 - k];
	}
	*out = v;
	return;
    }
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

    if (CSP_BITS_ALIGNED(pos, n)) {
	uint8_t* q = p + (pos >> 3);
	uint8_t nb = (uint8_t)(n >> 3);
	const uint8_t* s = (const uint8_t*)&v;   // s[0] is the low byte
	if (!be)
	    memcpy(q, s, nb);
	else
	    for (k = 0; k < nb; k++)
		q[k] = s[nb - 1 - k];
	return;                             // whole bytes: nothing to preserve
    }
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
