// Test: struct implementing Printable protocol used in print

struct Point {
	int x;
	int y;
}

impl Printable for Point {
	fn to_string(self) -> string {
		return "({self.x}, {self.y})";
	}
}

fn main() -> int {
	Point origin = Point { x: 0, y: 0 };
	println("origin: {}", origin);
	return 0;
}
