// Struct with invalid field (no type) should fail

struct Point {
	x;
	int y;
}

fn main() -> int {
	return 0;
}
