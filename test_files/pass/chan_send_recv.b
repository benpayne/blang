// Channel declaration with send/recv method calls.
// recv() returns Option<T>: some(value) on success, none when closed+empty.

fn main() {
	chan<int> ch;
	ch.send(42);
	match ch.recv() {
		some(x) {
			println("got {}", x);
		}
		none {
			println("channel closed");
		}
	}
	ch.close();
}
