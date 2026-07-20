/* runtime/blang_math.c — BLang math module C backing (libm wrappers).
 * Links libm (-lm). Pure functions: no heap, no refcounts. */
#include "blang_math.h"

#include <math.h>

double __blang_math_sqrt( double x )            { return sqrt( x ); }
double __blang_math_pow( double base, double e ) { return pow( base, e ); }
double __blang_math_sin( double x )             { return sin( x ); }
double __blang_math_cos( double x )             { return cos( x ); }
double __blang_math_tan( double x )             { return tan( x ); }
double __blang_math_log( double x )             { return log( x ); }
double __blang_math_log10( double x )           { return log10( x ); }
double __blang_math_exp( double x )             { return exp( x ); }
double __blang_math_floor( double x )           { return floor( x ); }
double __blang_math_ceil( double x )            { return ceil( x ); }
double __blang_math_fabs( double x )            { return fabs( x ); }

int __blang_math_abs_int( int x )
{
	return x < 0 ? -x : x;
}
