// Spawn as expression assigned to a Task variable

fn main() {
	Task t = spawn {
		int x = 42;
	};
}
