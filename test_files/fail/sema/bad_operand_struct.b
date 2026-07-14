// U4 diagnostic coverage: invalid operand to an arithmetic operator (struct).
struct Point { int x; int y; }
impl Point { init(int x, int y) { self.x = x; self.y = y; } }
fn main() -> int {
	Point p = Point(1, 2);
	int r = p + 1;
	return r;
}
