/* runtime/blang_time.h — BLang time module C backing.
 *
 * Wall-clock and monotonic time. All return `long` (no heap, no refcounts).
 * Wall-clock values are non-deterministic — the BLang codegen test asserts
 * invariants, never a raw value. See stdlib/time.b. */
#ifndef BLANG_TIME_H
#define BLANG_TIME_H

#include <stdint.h>

int64_t __blang_time_now( void );            /* Unix epoch seconds */
int64_t __blang_time_now_millis( void );     /* Unix epoch milliseconds */
int64_t __blang_time_monotonic_nanos( void ); /* CLOCK_MONOTONIC nanoseconds */

#endif /* BLANG_TIME_H */
