/* Unit tests for runtime/blang_time.c. Wall-clock values are non-deterministic,
 * so we assert invariants, never exact values. */
#include "test_util.h"
#include "../blang_time.h"

static void t_now_positive( void )
{
	/* After 2001-09-09 (epoch 1_000_000_000); any real clock is well past it. */
	CHECK( __blang_time_now() > 1000000000LL );
}
static void t_millis_ge_seconds( void )
{
	int64_t s = __blang_time_now();
	int64_t ms = __blang_time_now_millis();
	/* ms/1000 should equal the second reading within a 2s window. */
	CHECK( ms / 1000 >= s - 2 && ms / 1000 <= s + 2 );
}
static void t_monotonic_nondecreasing( void )
{
	int64_t a = __blang_time_monotonic_nanos();
	int64_t b = __blang_time_monotonic_nanos();
	CHECK( b >= a );          /* monotonic clock never goes backwards */
	CHECK( a > 0 );
}

static blang_test_case cases[] = {
	{ "now_positive", t_now_positive },
	{ "millis_ge_seconds", t_millis_ge_seconds },
	{ "monotonic_nondecreasing", t_monotonic_nondecreasing },
};
TEST_MAIN( cases )
