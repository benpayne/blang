/* runtime/blang_random.c — BLang random module C backing (SplitMix64 PRNG).
 *
 * SplitMix64 is tiny, fast, and fully specified, so a given seed yields the same
 * 64-bit stream on every platform — the property the seeded golden test relies
 * on. State is a process-global 64-bit counter (single-threaded use, matching
 * the rest of the stdlib). */
#include "blang_random.h"

static uint64_t g_state = 0x9E3779B97F4A7C15ULL; /* default (unseeded) state */

/* One SplitMix64 step: advance state, return the mixed output. */
static uint64_t next_u64( void )
{
	uint64_t z = ( g_state += 0x9E3779B97F4A7C15ULL );
	z = ( z ^ ( z >> 30 ) ) * 0xBF58476D1CE4E5B9ULL;
	z = ( z ^ ( z >> 27 ) ) * 0x94D049BB133111EBULL;
	return z ^ ( z >> 31 );
}

void __blang_random_seed( int64_t seed )
{
	g_state = (uint64_t)seed;
}

int64_t __blang_random_next( void )
{
	/* Mask to 63 bits so the result is always non-negative. */
	return (int64_t)( next_u64() & 0x7FFFFFFFFFFFFFFFULL );
}

int64_t __blang_random_int_range( int64_t lo, int64_t hi )
{
	if ( hi <= lo )
		return lo;                     /* empty/invalid range: clamp to lo */
	uint64_t span = (uint64_t)( hi - lo );
	return lo + (int64_t)( next_u64() % span );
}

double __blang_random_float01( void )
{
	/* Top 53 bits → uniform in [0,1); 2^53 is the double mantissa width. */
	uint64_t bits = next_u64() >> 11;
	return (double)bits / (double)( 1ULL << 53 );
}
