// LSP fixture: definition targets — variables, functions, struct
// construction, fields, and methods.
struct Point {
	int x;
	int y;
}

impl Point {
	init(int px, int py) {
		self.x = px;
		self.y = py;
	}

	fn norm1(self) -> int {
		return self.x + self.y;
	}
}

fn add(int a, int b) -> int {
	return a + b;
}

fn main() -> int {
	int first = 10;
	int second = add(first, 32);
	Point p = Point(1, 2);
	int nx = p.x;
	int nn = p.norm1();
	return second + nx + nn;
}
