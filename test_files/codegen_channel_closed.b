// End-to-end codegen test for the channel closed/empty signal.
// After draining a closed channel, recv() returns none so callers can detect
// end-of-stream rather than receiving a bogus zero value.

fn main() -> int {
	chan<int> ch;
	ch.send(7);
	ch.close();

	int got = 0;
	int missing = 0;

	// First recv: a value is buffered -> some(7)
	match ch.recv() {
		some(v) { got = v; }
		none { return 1; }
	}

	// Second recv: channel is closed and empty -> none
	match ch.recv() {
		some(v) { return 2; }
		none { missing = 1; }
	}

	// got == 7 and missing == 1 -> success
	return (got - 7) + (missing - 1);
}
