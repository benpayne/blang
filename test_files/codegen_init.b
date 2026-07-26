// Test: init constructors and static methods

extern fn printf(cstring fmt, ...) -> int;

struct Point {
	int x;
	int y;
}

impl Point {
	init(int x, int y) {
		self.x = x;
		self.y = y;
	}

	fn sum(self) -> int {
		return self.x + self.y;
	}

	static fn origin() -> Point {
		return Point(0, 0);
	}

	static fn from_value(int v) -> Point {
		return Point(v, v);
	}
}

fn main() -> int {
	// Test constructor call
	Point p = Point(3, 4);
	if p.x != 3 { printf("FAIL: p.x = %d, expected 3\n", p.x); return 1; }
	if p.y != 4 { printf("FAIL: p.y = %d, expected 4\n", p.y); return 1; }

	// Test method on constructed struct
	int s = p.sum();
	if s != 7 { printf("FAIL: sum = %d, expected 7\n", s); return 1; }

	// Test static method
	Point o = Point.origin();
	if o.x != 0 { printf("FAIL: origin.x = %d, expected 0\n", o.x); return 1; }
	if o.y != 0 { printf("FAIL: origin.y = %d, expected 0\n", o.y); return 1; }

	// Test static method with args
	Point f = Point.from_value(5);
	if f.x != 5 { printf("FAIL: from_value.x = %d, expected 5\n", f.x); return 1; }
	if f.y != 5 { printf("FAIL: from_value.y = %d, expected 5\n", f.y); return 1; }

	// Test constructor with expressions
	Point q = Point(1 + 2, 3 * 4);
	if q.x != 3 { printf("FAIL: q.x = %d, expected 3\n", q.x); return 1; }
	if q.y != 12 { printf("FAIL: q.y = %d, expected 12\n", q.y); return 1; }

	printf("All init/static tests passed!\n");
	return 0;
}
