// Basic wait statement

fn main() {
	Task t = spawn {
		int x = 42;
	};
	wait t;
}
