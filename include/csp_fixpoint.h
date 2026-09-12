// csp_fixpoint.h - Q16.16 fixed-point arithmetic
// Upper 16 bits: integer part (signed: -32768 to 32767)
// Lower 16 bits: fractional part (precision: 1/65536 ≈ 0.000015)

#ifndef __CSP_FIXPOINT_H__
#define __CSP_FIXPOINT_H__

#include <stdint.h>

#define FIX_SHIFT 16
// (int32_t)1, not 1. On AVR an `int` is SIXTEEN bits, so `1 << 16` is undefined
// and gcc folds it to zero -- with a warning that scrolls past in every build.
// FIX_SCALE is then 0, and everything derived from it goes quietly wrong on the
// one family that actually runs this code: FIX_CONST turns every float literal
// into 0, fix_round adds nothing and truncates instead of rounding, and
// FIX_MASK becomes all ones. Nothing changes on a 32-bit target, where the two
// spellings are the same constant.
#define FIX_SCALE ((int32_t)1 << FIX_SHIFT)   // 65536
#define FIX_MASK  (FIX_SCALE - 1)             // 0xFFFF

// Type for fixed-point values
typedef int32_t fixpoint_t;

// Convert integer to fixed-point
#define FIX_FROM_INT(i)   ((fixpoint_t)(i) << FIX_SHIFT)

// Convert fixed-point to integer (truncate)
#define FIX_TO_INT(f)     fix_trunc(f)

// Convert fixed-point to integer (round)
#define FIX_TO_INT_RND(f) fix_round(f)

// Convert float literal to fixed-point at compile time
#define FIX_CONST(x)      ((fixpoint_t)((x) * FIX_SCALE + ((x) >= 0 ? 0.5 : -0.5)))

// Basic arithmetic
#define FIX_ADD(a, b)     fix_add((a), (b))
#define FIX_SUB(a, b)     fix_sub((a),(b))
#define FIX_NEG(a)        fix_neg((a))
#define FIX_MUL(a, b)     fix_mul((a),(b))
#define FIX_DIV(a, b)     fix_div((a),(b))

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

extern int32_t fix_trunc(fixpoint_t f);
extern int32_t fix_round(fixpoint_t f);

extern fixpoint_t fix_neg(fixpoint_t a);
extern fixpoint_t fix_add(fixpoint_t a, fixpoint_t b);
extern fixpoint_t fix_sub(fixpoint_t a, fixpoint_t b);

extern fixpoint_t fix_mul(fixpoint_t a, fixpoint_t b);
extern fixpoint_t fix_div(fixpoint_t a, fixpoint_t b);

extern fixpoint_t fix_sqrt(fixpoint_t x);
#endif // __CSP_FIXPOINT_H__
