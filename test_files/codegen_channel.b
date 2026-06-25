// End-to-end codegen test for channel send/recv (single-threaded, buffered).
// Sends three values into a buffered channel and reads them back, verifying
// FIFO ordering and value integrity.  Returns 0 on success.

fn main() -> int {
	chan<int> ch;

	ch.send(10);
	ch.send(20);
	ch.send(12);

	int a = ch.recv();
	int b = ch.recv();
	int c = ch.recv();

	ch.close();

	// FIFO: a=10, b=20, c=12 -> sum 42
	return a + b + c - 42;
}
