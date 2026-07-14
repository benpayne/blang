// audit_06 (design.md "The 10 audit programs"): mutating a field through a
// `shared` value. Today allowed (shared structs are mutable through fields);
// U7 rejects it — shared values are immutable through fields, use `sync` for
// mutable shared state.
struct Point { int x; int y; }

impl Point {
	init(int x, int y) {
		self.x = x;
		self.y = y;
	}
}

fn main() -> int {
	shared Point p = Point(1, 2);
	p.x = 9;
	return 0;
}
