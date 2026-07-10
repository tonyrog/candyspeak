// csp_fixpoint.h - Q16.16 fixed-point arithmetic
// Upper 16 bits: integer part (signed: -32768 to 32767)
// Lower 16 bits: fractional part (precision: 1/65536 ≈ 0.000015)

#ifndef __CSP_FIXPOINT_H__
#define __CSP_FIXPOINT_H__

#include <stdint.h>

#define FIX_SHIFT 16
#define FIX_SCALE (1 << FIX_SHIFT)       // 65536
#define FIX_MASK  (FIX_SCALE - 1)        // 0xFFFF

// Type for fixed-point values
typedef int32_t fixpoint_t;

// Convert integer to fixed-point
#define FIX_FROM_INT(i)   ((fixpoint_t)(i) << FIX_SHIFT)

// Convert fixed-point to integer (truncate)
#define FIX_TO_INT(f)     ((int32_t)(f) >> FIX_SHIFT)

// Convert fixed-point to integer (round)
#define FIX_TO_INT_RND(f) (((f) + (FIX_SCALE/2)) >> FIX_SHIFT)

// Convert float literal to fixed-point at compile time
#define FIX_CONST(x)      ((fixpoint_t)((x) * FIX_SCALE + ((x) >= 0 ? 0.5 : -0.5)))

// Basic arithmetic
#define FIX_ADD(a, b)     ((a) + (b))
#define FIX_SUB(a, b)     ((a) - (b))
#define FIX_NEG(a)        (-(a))

// Multiplication: (a * b) >> 16
// Use 64-bit intermediate to avoid overflow
#define FIX_MUL(a, b)     ((fixpoint_t)(((int64_t)(a) * (int64_t)(b)) >> FIX_SHIFT))

// Division: (a << 16) / b
// Use 64-bit intermediate to avoid overflow
#define FIX_DIV(a, b)     ((fixpoint_t)(((int64_t)(a) << FIX_SHIFT) / (b)))

// Comparisons (same as integer comparisons)
#define FIX_LT(a, b)      ((a) < (b))
#define FIX_LTE(a, b)     ((a) <= (b))
#define FIX_GT(a, b)      ((a) > (b))
#define FIX_GTE(a, b)     ((a) >= (b))
#define FIX_EQ(a, b)      ((a) == (b))
#define FIX_NEQ(a, b)     ((a) != (b))

// Absolute value
#define FIX_ABS(a)        ((a) >= 0 ? (a) : -(a))

// Common constants
#define FIX_ZERO          0
#define FIX_ONE           FIX_SCALE                    // 1.0
#define FIX_HALF          (FIX_SCALE / 2)              // 0.5
#define FIX_PI            FIX_CONST(3.14159265358979)  // π
#define FIX_2PI           FIX_CONST(6.28318530717959)  // 2π
#define FIX_PI_2          FIX_CONST(1.57079632679490)  // π/2
#define FIX_E             FIX_CONST(2.71828182845905)  // e

// Convert to/from float (runtime, for I/O only)
// static inline fixpoint_t fix_from_float(float f) {
//    return (fixpoint_t)(f * FIX_SCALE + (f >= 0 ? 0.5f : -0.5f));
//}

// static inline float fix_to_float(fixpoint_t f) {
//     return (float)f / FIX_SCALE;
// }

// Integer sqrt for fixed-point (result is Q16.16)
static inline fixpoint_t fix_sqrt(fixpoint_t x)
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

#endif // __CSP_FIXPOINT_H__
