// Demo 14: Timer event loop
//
// Registers a repeating timer on the global event loop. The handler runs every
// 200ms and stops the loop after 5 ticks. No timer.run() call is needed — once
// main() finishes, the event loop runs automatically because an `on` handler is
// registered, and exits when the handler calls timer.stop().
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

	println("event loop starting (runs automatically)...");
	// main() falls through here; the event loop runs until timer.stop().
}
