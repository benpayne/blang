// Auto-run: main registers an `on` handler and falls through — the event loop
// runs automatically (no timer.run()). The handler verifies the fire count and
// exits with the result.
import timer;
import sys;
fn main() {
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
