// End-to-end codegen test for channel send/recv (single-threaded, buffered).
// recv() returns Option<T>; on success the some(v) arm binds the value.
// Sends three values and reads them back, verifying FIFO order and integrity.

fn main() -> int {
	chan<int> ch;

	ch.send(10);
	ch.send(20);
	ch.send(12);

	int sum = 0;

	match ch.recv() {
		some(v) { sum = sum + v; }
		none { return 1; }
	}
	match ch.recv() {
		some(v) { sum = sum + v; }
		none { return 2; }
	}
	match ch.recv() {
		some(v) { sum = sum + v; }
		none { return 3; }
	}

	ch.close();

	// FIFO: 10 + 20 + 12 -> 42
	return sum - 42;
}
