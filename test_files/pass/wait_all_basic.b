// Basic wait_all statement

fn main() {
	spawn {
		int x = 1;
	};
	spawn {
		int y = 2;
	};
	wait_all;
}
