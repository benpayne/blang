// E2E test: struct implementing Printable used in println
// Tests: struct with to_string method used in {} placeholder

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
	Point p = Point { x: 10, y: 20 };
	println("point: {}", p);

	println("print printable codegen test passed!");
	return 0;
}
