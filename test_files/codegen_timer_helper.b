// Auto-run with the `on` handler buried in a helper function (not in main).
// main() only calls setup(); the handler is registered indirectly. The auto-run
// injection into main must be driven by a module-wide pre-scan, not by whether
// the `on` is lexically inside main — so this exercises order-independence.
// The handler exits 0 on the 3rd tick.
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
	setup();
	// main falls through; the event loop runs automatically because setup()
	// registered an `on` handler. Without the pre-scan, no run_auto() would be
	// injected here and the program would exit before any tick fired.
}
