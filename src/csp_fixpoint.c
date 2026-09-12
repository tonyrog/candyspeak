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
fixpoint_t fix_div(fixpoint_t a, fixpoint_t b)
{
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
