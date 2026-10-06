// csp_lib -- the inner core. See include/csp_lib.h.

#include <stdint.h>
#include "csp_lib.h"

static uint8_t lib_res(uint8_t res)
{
    if (res < 2)  return 2;
    if (res > 16) return 16;
    return res;
}

int32_t csp_lib_ain(uint16_t raw, uint8_t res, uint8_t sgn)
{
    int32_t v;

    res = lib_res(res);
    v = (int32_t)(raw >> (16 - res));
    if (sgn)
	v -= (int32_t)1 << (res - 1);          // 0 = mid scale
    return v;
}

// The mirror of csp_lib_ain, so a value read in and written straight back out
// lands where it came from.
uint16_t csp_lib_aout(uint32_t val, uint8_t res, uint8_t sgn)
{
    uint32_t v;

    res = lib_res(res);
    v = val;
    if (sgn)
	v += (uint32_t)1 << (res - 1);
    v &= ((uint32_t)1 << res) - 1;
    return (uint16_t)(v << (16 - res));
}

void csp_lib_timer_in(csp_timer_t* in, csp_timer_t* out, uint32_t now)
{
    in->fired = out->fired = 0;
    if (in->running && ((uint32_t)(now - in->t0) >= in->period)) {
	in->running = out->running = 0;
	in->val = out->val = 0;
	in->fired = out->fired = 1;
    }
}

void csp_lib_timer_out(csp_timer_t* in, csp_timer_t* out, uint32_t now)
{
    if (!in->running && in->val) {
	in->running = out->running = 1;
	in->fired = out->fired = 0;
	in->t0 = out->t0 = now;
    }
}

__attribute__((weak)) void csp_lib_putc(char c)
{
    (void)c;
}

void csp_lib_puts(const char* s)
{
    while (*s)
	csp_lib_putc(*s++);
}

void csp_lib_putu(uint32_t v)
{
    char buf[10];
    int n = 0;

    do {
	buf[n++] = (char)('0' + (v % 10));
	v /= 10;
    } while (v);
    while (n)
	csp_lib_putc(buf[--n]);
}

void csp_lib_puti(int32_t v)
{
    if (v < 0) {
	csp_lib_putc('-');
	csp_lib_putu(0u - (uint32_t)v);
    }
    else
	csp_lib_putu((uint32_t)v);
}

void csp_lib_nl(void)
{
    csp_lib_putc('\n');
}
