// E2E test: timer event loop. `on timer.every(ms) { }` registers a handler on
// the global event loop; the handler fires repeatedly until it stops the loop.
// A captured `sync` counter (shared heap state) records the number of fires.

import timer;

fn main() -> int {
	sync int count = 0;

	on timer.every(5) {
		count = count + 1;
		if count >= 3 {
			timer.stop();
		}
	}

	timer.run();

	// The handler fired exactly 3 times before stopping the loop.
	if count == 3 {
		return 0;
	}
	return 1;
}
