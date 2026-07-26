// stdlib/time.b — Time module: wall-clock and monotonic clock reads.
//
// Usage: import time;
//   time.now()             -> long  (Unix epoch seconds)
//   time.now_millis()      -> long  (Unix epoch milliseconds)
//   time.monotonic_nanos() -> long  (CLOCK_MONOTONIC ns; for interval timing)
//
// Scope note (U4): date formatting (strftime / a Duration type) is deferred to
// a later stdlib tier — U4 ships the three clock primitives, which satisfy
// REQ-005's time/date clause via epoch/monotonic reads. Wall-clock values are
// non-deterministic; callers/tests assert invariants, never a raw timestamp.

extern fn __blang_time_now() -> long;
extern fn __blang_time_now_millis() -> long;
extern fn __blang_time_monotonic_nanos() -> long;

pub fn now() -> long { return __blang_time_now(); }
pub fn now_millis() -> long { return __blang_time_now_millis(); }
pub fn monotonic_nanos() -> long { return __blang_time_monotonic_nanos(); }
