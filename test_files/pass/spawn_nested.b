// Nested spawn statements

fn main() {
	spawn {
		int x = 1;
		spawn {
			int y = 2;
		}
	}
}
