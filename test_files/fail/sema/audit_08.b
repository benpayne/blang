// audit_08 (design.md "The 10 audit programs"): a generic function constrained by
// a protocol, instantiated with a type that does not satisfy the constraint. Today
// the constraint is decorative; U5 rejects the instantiation at the semantic stage.
protocol Comparable {
	fn compare(self) -> int;
}

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

fn smallest<T: Comparable>(T a, T b) -> T {
	return a;
}

fn main() -> int {
	Point p = Point(1, 2);
	Point q = Point(3, 4);
	Point r = smallest<Point>(p, q);
	return 0;
}
