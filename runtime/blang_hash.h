/* runtime/blang_hash.h — BLang hash module C backing.
 *
 * A single FNV-1a hash over a BlangString's bytes, used by the hashed
 * collections (Map/Set) in stdlib/collections.b. Returns a non-negative 63-bit
 * hash. No heap allocation retained (frees its own cstring copy). */
#ifndef BLANG_HASH_H
#define BLANG_HASH_H

#include "blang_string.h"

#include <stdint.h>

/* FNV-1a over the key's bytes, masked to 31 bits so the result is a
 * non-negative BLang `int` (i32) — lets the hashed collections index buckets
 * without a narrowing cast (BLang has no C-style cast). NULL key -> 0. */
int32_t __blang_hash_string( BlangString *s );

#endif /* BLANG_HASH_H */
