// Unknown struct field access is rejected by the semantic pass with a located
// error naming the field (U3, FR-006). Correct BLang: struct + init constructor.
struct Point {
	int x;
	int y;
}

impl Point {
	init(int x, int y) {
		self.x = x;
		self.y = y;
	}
}

fn main() -> int {
	Point p = Point(1, 2);
	return p.nonexistent;
}
