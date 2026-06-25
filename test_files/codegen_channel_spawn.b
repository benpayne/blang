// End-to-end codegen test for channels across a spawn boundary.
// A spawned producer sends a value; main blocks in recv() until it arrives.
// Exercises channel capture into a spawn closure and blocking handoff.

fn main() -> int {
	chan<int> ch;

	spawn {
		ch.send(42);
	}

	int x = ch.recv();
	return x - 42;
}
