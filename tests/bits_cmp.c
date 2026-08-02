// Prove csp_bits.h agrees with bitpack.h bit for bit, over every position and
// width the view encoding can express, in both orders, against a buffer that
// already has content (so "leaves the surrounding bits alone" is tested too).
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "bitpack.h"
#include "csp_bits.h"

#define NB 72          /* bits of headroom: 64-byte frame + slack */
static int fails = 0;

static void cmp_one(uint16_t pos, uint8_t n, int be, uint32_t val, uint8_t seed)
{
    uint8_t a[NB/8+8], b[NB/8+8];
    uint32_t va = 0, vb = 0;
    unsigned i;

    for (i = 0; i < sizeof(a); i++) a[i] = b[i] = (uint8_t)(seed * 31u + i * 7u);

    if (be) set_bits_be(a, val, pos, n); else set_bits_le(a, val, pos, n);
    csp_bits_set(b, val, pos, n, be);
    if (memcmp(a, b, sizeof(a)) != 0) {
	printf("SET differ pos=%u n=%u be=%d val=%08x\n", pos, n, be, val);
	if (++fails > 10) exit(1);
	return;
    }
    if (be) get_bits_be(a, &va, pos, n); else get_bits_le(a, &va, pos, n);
    csp_bits_get(b, &vb, pos, n, be);
    if (va != vb) {
	printf("GET differ pos=%u n=%u be=%d: %08x vs %08x\n", pos, n, be, va, vb);
	if (++fails > 10) exit(1);
    }
}

int main(void)
{
    static const uint32_t vals[] = { 0, 1, 2, 0x5a, 0xff, 0x1234, 0xdeadbeefu,
				     0x80000000u, 0xffffffffu, 0x55555555u };
    uint16_t pos; uint8_t n; int be; unsigned v, s;

    for (be = 0; be < 2; be++)
	for (pos = 0; pos < 40; pos++)
	    for (n = 1; n <= 32; n++)
		for (v = 0; v < sizeof(vals)/sizeof(vals[0]); v++)
		    for (s = 0; s < 3; s++)
			cmp_one(pos, n, be, vals[v], (uint8_t)s);
    printf(fails ? "FAIL (%d)\n" : "ok, identical\n", fails);
    return fails != 0;
}
