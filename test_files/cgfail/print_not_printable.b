// Fail test: struct without Printable used in print placeholder

struct Point {
	int x;
	int y;
}

fn main() -> int {
	Point p = Point { x: 1, y: 2 };
	println("{}", p);
	return 0;
}
