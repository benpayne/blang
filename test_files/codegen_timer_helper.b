// Handler registered from a helper, loop entered from main: registration and
// run() can live in different functions. setup() registers an `on` handler;
// main() calls setup() and then explicitly enters the loop with timer.run().
// The handler exits 0 on the 3rd tick (and 1 if it ever overruns).
import timer;
import sys;

fn setup() {
	sync int count = 0;
	on timer.every(5) {
		count = count + 1;
		if count == 3 {
			sys.exit(0);
		}
		if count > 3 {
			sys.exit(1);
		}
	}
}

fn main() {
	setup();      // registers the handler (no firing yet)
	timer.run();  // enter the loop — the handler fires from here
}
