/* Unit tests for runtime/blang_random.c (SplitMix64 PRNG).
 * Determinism is asserted structurally (reproducibility + bounds); the exact
 * seeded sequence is pinned by the codegen golden (codegen_random.b). */
#include "test_util.h"
#include "../blang_random.h"

static void t_seeded_sequence( void )
{
	__blang_random_seed( 1 );
	int64_t a = __blang_random_next();
	int64_t b = __blang_random_next();
	int64_t c = __blang_random_next();
	CHECK( a >= 0 && b >= 0 && c >= 0 );      /* non-negative (63-bit) */
	CHECK( !( a == b && b == c ) );           /* not a degenerate constant */
}
static void t_seed_reproducible( void )
{
	__blang_random_seed( 123456789 );
	int64_t first[8];
	for ( int i = 0; i < 8; i++ ) first[i] = __blang_random_next();
	__blang_random_seed( 123456789 );
	for ( int i = 0; i < 8; i++ )
		CHECK_EQ_I( __blang_random_next(), first[i] ); /* same seed => same stream */
}
static void t_int_range_bounds( void )
{
	__blang_random_seed( 7 );
	for ( int i = 0; i < 1000; i++ )
	{
		int64_t v = __blang_random_int_range( 10, 20 );
		CHECK( v >= 10 && v < 20 );
	}
}
static void t_int_range_empty( void )
{
	CHECK_EQ_I( __blang_random_int_range( 5, 5 ), 5 );   /* empty range clamps to lo */
	CHECK_EQ_I( __blang_random_int_range( 9, 3 ), 9 );   /* inverted range clamps to lo */
}
static void t_float01_bounds( void )
{
	__blang_random_seed( 99 );
	for ( int i = 0; i < 1000; i++ )
	{
		double f = __blang_random_float01();
		CHECK( f >= 0.0 && f < 1.0 );
	}
}

static blang_test_case cases[] = {
	{ "seeded_sequence", t_seeded_sequence },
	{ "seed_reproducible", t_seed_reproducible },
	{ "int_range_bounds", t_int_range_bounds },
	{ "int_range_empty", t_int_range_empty },
	{ "float01_bounds", t_float01_bounds },
};
TEST_MAIN( cases )
