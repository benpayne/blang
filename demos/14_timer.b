// Demo 14: Timer event loop
//
// Registers a repeating timer on the global event loop, then enters the loop
// explicitly with timer.run(). The handler runs every 200ms and stops the loop
// after 5 ticks, at which point timer.run() returns and main() finishes.
//
// `on` only registers the handler — nothing fires until timer.run() is called,
// so control flow stays explicit (no hidden loop after main).
//
// Run: bcc demos/14_timer.b -o timer && ./timer

import timer;

fn main() {
	sync int ticks = 0;

	on timer.every(200) {
		ticks = ticks + 1;
		println("tick {}", ticks);
		if ticks >= 5 {
			timer.stop();
		}
	}

	println("event loop starting...");
	timer.run();   // blocks here until timer.stop() above; then returns
	println("event loop stopped");
}
