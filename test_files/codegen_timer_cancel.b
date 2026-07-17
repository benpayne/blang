// Cancel an individual timer by its source handle. After 2 ticks the handler
// cancels the timer; with no sources left the loop idle-exits.
import timer;
fn main() -> int {
	sync int count = 0;
	int t = timer.every(5);
	on t {
		count = count + 1;
		if count == 2 {
			timer.cancel(t);
		}
	}
	timer.run();
	if count == 2 {
		return 0;
	}
	return 1;
}
