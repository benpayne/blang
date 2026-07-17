// stdlib/timer.b — Timers and the event loop
//
// Usage: import timer;
//   on timer.every(1000) { ... }   // register a handler: run every 1000ms
//   on timer.after(500)  { ... }   // register a handler: run once after 500ms
//   timer.run();                   // enter the loop — handlers fire from here
//
// `on EXPR { ... }` only *registers* a handler; it does not fire it. Handlers
// run when you explicitly enter the loop with `timer.run()`, which blocks until
// either stop() is called or every source has been cancelled / has finished
// (e.g. all one-shot timers have fired). This keeps control flow explicit: code
// before run() executes to completion first, and nothing in your `main` body is
// silently preempted by a timer.
//
//   timer.stop()        // stop the whole loop (makes run() return)
//   int t = timer.every(1000); on t { ... } timer.cancel(t);  // cancel one timer
//
// timer.every/after return a source handle (a timerfd) that `on` keys on.

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

// Enter the global event loop. Blocks until stop() is called or every source
// has finished. Registered `on` handlers fire from here, not before.
pub fn run() {
	__blang_event_run();
}

// Stop the whole event loop.
pub fn stop() {
	__blang_event_stop();
}
