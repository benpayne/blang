// stdlib/timer.b — Timers and the event loop
//
// Usage: import timer;
//   on timer.every(1000) { ... }   // run the body every 1000ms
//   on timer.after(500)  { ... }   // run the body once after 500ms
//
// The event loop runs automatically once main() returns, as long as any `on`
// handler is registered — you do NOT need to call run(). The loop blocks until
// either stop() is called or every source has been cancelled / has finished
// (e.g. all one-shot timers have fired).
//
//   timer.stop()        // stop the whole loop
//   int t = timer.every(1000); on t { ... } timer.cancel(t);  // cancel one timer
//   timer.run()         // optional: run the loop explicitly (blocks)
//
// `on EXPR { ... }` registers the body on the global event loop, keyed by the
// fd that EXPR yields. timer.every/after return a source handle (a timerfd).

extern fn __blang_timer_every(int interval_ms) -> int;
extern fn __blang_timer_after(int delay_ms) -> int;
extern fn __blang_event_cancel(int source);
extern fn __blang_event_run();
extern fn __blang_event_stop();

// Repeating timer: yields a source that fires every `ms` milliseconds.
pub fn every(int ms) -> int {
	return __blang_timer_every(ms);
}

// One-shot timer: yields a source that fires once after `ms` milliseconds,
// then removes itself from the loop.
pub fn after(int ms) -> int {
	return __blang_timer_after(ms);
}

// Cancel an individual timer/source returned by every()/after(): it is removed
// from the loop and will no longer fire.
pub fn cancel(int source) {
	__blang_event_cancel(source);
}

// Run the global event loop explicitly. Usually unnecessary — the loop runs
// automatically after main() when handlers are registered.
pub fn run() {
	__blang_event_run();
}

// Stop the whole event loop.
pub fn stop() {
	__blang_event_stop();
}
