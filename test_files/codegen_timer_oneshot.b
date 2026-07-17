// One-shot timer + idle-exit: timer.after fires once, the loop has no remaining
// sources, so run() returns. (Previously run() blocked forever.)
import timer;
fn main() -> int {
	sync int fired = 0;
	on timer.after(5) {
		fired = fired + 1;
	}
	timer.run();
	if fired == 1 {
		return 0;
	}
	return 1;
}
