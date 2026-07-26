/* Unit tests for runtime/blang_math.c (libm wrappers). */
#include "test_util.h"
#include "../blang_math.h"

#include <math.h>

#define CLOSE(a,b) CHECK( fabs( (a) - (b) ) < 1e-9 )

static void t_sqrt( void )
{
	CLOSE( __blang_math_sqrt( 16.0 ), 4.0 );
	CLOSE( __blang_math_sqrt( 2.0 ), 1.4142135623730951 );
}
static void t_pow( void )
{
	CLOSE( __blang_math_pow( 2.0, 10.0 ), 1024.0 );
	CLOSE( __blang_math_pow( 9.0, 0.5 ), 3.0 );
}
static void t_trig( void )
{
	CLOSE( __blang_math_sin( 0.0 ), 0.0 );
	CLOSE( __blang_math_cos( 0.0 ), 1.0 );
	CLOSE( __blang_math_tan( 0.0 ), 0.0 );
}
static void t_log_exp( void )
{
	CLOSE( __blang_math_log( 1.0 ), 0.0 );
	CLOSE( __blang_math_log10( 1000.0 ), 3.0 );
	CLOSE( __blang_math_exp( 0.0 ), 1.0 );
}
static void t_floor_ceil( void )
{
	CLOSE( __blang_math_floor( 3.7 ), 3.0 );
	CLOSE( __blang_math_ceil( 3.2 ), 4.0 );
	CLOSE( __blang_math_floor( -1.5 ), -2.0 );
}
static void t_fabs( void )
{
	CLOSE( __blang_math_fabs( -5.5 ), 5.5 );
	CLOSE( __blang_math_fabs( 5.5 ), 5.5 );
}
static void t_abs_int( void )
{
	CHECK_EQ_I( __blang_math_abs_int( -7 ), 7 );
	CHECK_EQ_I( __blang_math_abs_int( 7 ), 7 );
	CHECK_EQ_I( __blang_math_abs_int( 0 ), 0 );
}

static blang_test_case cases[] = {
	{ "sqrt", t_sqrt },
	{ "pow", t_pow },
	{ "trig", t_trig },
	{ "log_exp", t_log_exp },
	{ "floor_ceil", t_floor_ceil },
	{ "fabs", t_fabs },
	{ "abs_int", t_abs_int },
};
TEST_MAIN( cases )
