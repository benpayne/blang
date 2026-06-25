// End-to-end codegen test for channels across a spawn boundary.
// A spawned producer sends a value; main blocks in recv() until it arrives,
// receiving some(42).  Exercises channel capture into a spawn closure, the
// blocking handoff, and Option<T> recv.

fn main() -> int {
	chan<int> ch;

	spawn {
		ch.send(42);
	}

	int result = 99;
	match ch.recv() {
		some(v) { result = v; }
		none { result = -1; }
	}

	return result - 42;
}
