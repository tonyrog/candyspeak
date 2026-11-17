// src/csp_fixpoint.c's IEEE <-> Q16.16 conversions against the host's own
// floats: a decimal literal to IEEE bits is strtof's, correctly rounded; Q16.16
// to IEEE is (float)(q/65536); IEEE to Q16.16 rounds to the nearest, halves
// away from zero; and a value with no more than 24 significant bits goes there
// and back exactly. These are what every node compiles and loads floats with
// (ROM format 23), in integers only.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
extern int32_t fix_from_ieee(uint32_t b);
extern int fix_ieee_exact(uint32_t b);
extern uint32_t fix_to_ieee(int32_t q);
extern uint32_t fix_ieee_decimal(uint32_t w, uint32_t n, uint32_t d);
static uint32_t bits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
static int sig24(int32_t q) { uint32_t a = q < 0 ? -(uint32_t)q : (uint32_t)q; while (a && !(a & 1)) a >>= 1; return a < (1u << 24); }
static float flt(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }
int main(void) {
    int bad = 0, i;
    srand(7);
    for (i = 0; i < 200000; i++) {          // decimal -> IEEE vs strtof
	uint32_t w = (i % 3 == 0) ? rand() % 100 : rand();
	int nd = 1 + rand() % 9; uint32_t d = 1, n; char buf[64];
	for (int k = 0; k < nd; k++) d *= 10;
	n = rand() % d;
	snprintf(buf, sizeof buf, "%u.%0*u", w, nd, n);
	if (fix_ieee_decimal(w, n, d) != bits(strtof(buf, 0))) { if (bad++ < 5) printf("dec %s %08x %08x\n", buf, fix_ieee_decimal(w,n,d), bits(strtof(buf,0))); }
    }
    for (i = 0; i < 200000; i++) {          // Q16.16 -> IEEE vs (float)
	int32_t q = (int32_t)((uint32_t)rand() * 2654435761u); if (i & 1) q >>= (rand() % 20);
	float want = (float)((double)q / 65536.0);
	if (fix_to_ieee(q) != bits(want)) { if (bad++ < 10) printf("to %d\n", q); }
	// and back, when exact
	if (sig24(q) && (!fix_ieee_exact(fix_to_ieee(q)) || fix_from_ieee(fix_to_ieee(q)) != q)) { if (bad++ < 15) printf("back %d\n", q); }
    }
    for (i = 0; i < 200000; i++) {          // IEEE -> Q16.16 vs rounding
	float f = ((float)rand() / RAND_MAX - 0.5f) * 70000.0f;
	double v = (double)f * 65536.0;
	int32_t want = (v >= 2147483647.0) ? 0x7fffffff : (v <= -2147483647.0) ? -0x7fffffff : (int32_t)(v < 0 ? -floor(-v + 0.5) : floor(v + 0.5));
	if (fix_from_ieee(bits(f)) != want) { if (bad++ < 20) printf("from %g %d %d\n", f, fix_from_ieee(bits(f)), want); }
    }
    printf("%s, %d bad\n", bad ? "FAIL" : "ok", bad);
    return bad != 0;
}
