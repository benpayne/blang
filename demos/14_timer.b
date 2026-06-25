// Demo 14: Timer event loop
//
// Registers a repeating timer on the global event loop. The handler runs every
// 200ms and stops the loop after 5 ticks. `on EXPR { ... }` registers the body
// against the event source that EXPR yields (here, a timer).
//
// Run: bcc demos/14_timer.b -o timer && ./timer

import timer;

fn main() -> int {
	sync int ticks = 0;

	on timer.every(200) {
		ticks = ticks + 1;
		println("tick {}", ticks);
		if ticks >= 5 {
			timer.stop();
		}
	}

	println("starting event loop...");
	timer.run();
	println("done after {} ticks", ticks);
	return 0;
}
