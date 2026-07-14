// audit_10: unknown struct field access. Today a silent nullptr, statement dropped.
struct Point { int x; int y; }
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
