// Unknown method call is rejected by the semantic pass with a located error
// naming the method (U3, FR-007). frobnicate() is neither a method nor a field.
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
	return p.frobnicate();
}
