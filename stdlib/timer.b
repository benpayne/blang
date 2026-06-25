// stdlib/timer.b — Timers and the event loop
//
// Usage: import timer;
//   on timer.every(1000) { ... }   // run the body every 1000ms
//   on timer.after(500)  { ... }   // run the body once after 500ms
//   timer.run()                    // run the event loop (blocks)
//   timer.stop()                   // stop the event loop
//
// `on EXPR { ... }` registers the body on the global event loop, keyed by the
// fd that EXPR yields. timer.every/after return a timerfd for exactly this.

extern fn __blang_timer_every(int interval_ms) -> int;
extern fn __blang_timer_after(int delay_ms) -> int;
extern fn __blang_event_run();
extern fn __blang_event_stop();

// Repeating timer: yields a source that fires every `ms` milliseconds.
pub fn every(int ms) -> int {
	return __blang_timer_every(ms);
}

// One-shot timer: yields a source that fires once after `ms` milliseconds.
pub fn after(int ms) -> int {
	return __blang_timer_after(ms);
}

// Run the global event loop. Blocks the calling thread until stop() is called
// (typically from within an `on` handler).
pub fn run() {
	__blang_event_run();
}

// Stop the global event loop.
pub fn stop() {
	__blang_event_stop();
}
