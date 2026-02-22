// Test matching on enum variants with associated types

enum Color {
	red,
	green,
	blue,
	custom(int, int, int)
}

fn color_name(int c) -> int {
	match c {
		0 {
			return 1;
		}
		1 {
			return 2;
		}
		_ {
			return 0;
		}
	}
}

fn main() -> int {
	return 0;
}
