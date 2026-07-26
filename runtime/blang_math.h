/* runtime/blang_math.h — BLang math module C backing (libm wrappers).
 *
 * Thin, side-effect-free wrappers over libm so the BLang `math` module can call
 * them via `extern fn`. All operate on/return `double` (no heap, no refcounts,
 * so the owned-return contract does not apply). See stdlib/math.b for the
 * idiomatic BLang surface. */
#ifndef BLANG_MATH_H
#define BLANG_MATH_H

double __blang_math_sqrt( double x );
double __blang_math_pow( double base, double exp );
double __blang_math_sin( double x );
double __blang_math_cos( double x );
double __blang_math_tan( double x );
double __blang_math_log( double x );
double __blang_math_log10( double x );
double __blang_math_exp( double x );
double __blang_math_floor( double x );
double __blang_math_ceil( double x );
double __blang_math_fabs( double x );
/* int (i32) to match BLang `int` — a `long` param here would be an ABI mismatch
 * (BLang passes a 32-bit arg; the upper bits would be garbage). */
int    __blang_math_abs_int( int x );

#endif /* BLANG_MATH_H */
