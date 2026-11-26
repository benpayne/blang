/* runtime/blang_hash.h — BLang hash module C backing.
 *
 * FNV-1a hashes used by the hashed collections (Map/Set) in
 * stdlib/collections.b. __blang_hash_string hashes a BlangString's bytes;
 * __blang_hash_bits hashes the raw bytes of a value-type key (integers and
 * bitcast floats). Both return a non-negative hash. */
#ifndef BLANG_HASH_H
#define BLANG_HASH_H

#include "blang_string.h"

#include <stdint.h>

/* FNV-1a over the key's bytes, masked to 31 bits so the result is a
 * non-negative BLang `int` (i32) — lets the hashed collections index buckets
 * without a narrowing cast (BLang has no C-style cast). NULL key -> 0. */
int32_t __blang_hash_string( BlangString *s );

/* FNV-1a over the 8 raw bytes of a value-type key (integers sign/zero-extended
 * to i64, floats bitcast). Masked to 31 bits like the string hash. */
int32_t __blang_hash_bits( int64_t v );

#endif /* BLANG_HASH_H */
