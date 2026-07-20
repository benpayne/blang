// stdlib/random.b — Random module: seedable, deterministic PRNG (SplitMix64).
//
// Usage: import random;
//   random.seed(n)             -> set the PRNG state (fixed seed => fixed stream)
//   random.next()              -> long   (non-negative 63-bit)
//   random.int_range(lo, hi)   -> long   ([lo, hi), half-open)
//   random.float01()           -> double ([0.0, 1.0))
//
// A dedicated C PRNG (NOT libc rand()), so a given seed produces the same
// sequence on every platform — the property the seeded golden test relies on.

extern fn __blang_random_seed(long seed);
extern fn __blang_random_next() -> long;
extern fn __blang_random_int_range(long lo, long hi) -> long;
extern fn __blang_random_float01() -> double;

pub fn seed(long s) { __blang_random_seed(s); }
pub fn next() -> long { return __blang_random_next(); }
pub fn int_range(long lo, long hi) -> long { return __blang_random_int_range(lo, hi); }
pub fn float01() -> double { return __blang_random_float01(); }
