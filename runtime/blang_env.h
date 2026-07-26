/* runtime/blang_env.h — BLang env module C backing (getenv).
 *
 * Reads process environment variables. __blang_env_get returns a freshly
 * created BlangString (owned: +1 reference the caller releases) when the var is
 * set, or NULL when unset — the BLang wrapper maps NULL → Option.none. See
 * stdlib/env.b. */
#ifndef BLANG_ENV_H
#define BLANG_ENV_H

#include "blang_string.h"

#include <stdbool.h>

/* Returns a new (+1) BlangString with the value, or NULL if the var is unset. */
BlangString *__blang_env_get( BlangString *name );

/* True iff the named variable is present in the environment. */
bool __blang_env_has( BlangString *name );

#endif /* BLANG_ENV_H */
