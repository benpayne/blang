/* runtime/blang_random.h — BLang random module C backing.
 *
 * A dedicated, self-contained PRNG (SplitMix64) so a fixed seed produces a
 * fixed, platform-stable sequence — NOT libc rand() (whose sequence varies by
 * platform). This makes the BLang random codegen test golden-checkable with a
 * seeded stream. No heap, no refcounts. See stdlib/random.b. */
#ifndef BLANG_RANDOM_H
#define BLANG_RANDOM_H

#include <stdint.h>

void    __blang_random_seed( int64_t seed );          /* set PRNG state */
int64_t __blang_random_next( void );                  /* next 63-bit non-negative */
int64_t __blang_random_int_range( int64_t lo, int64_t hi ); /* [lo, hi) */
double  __blang_random_float01( void );               /* [0.0, 1.0) */

#endif /* BLANG_RANDOM_H */
