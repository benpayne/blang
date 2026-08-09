// modules-v2-exports U5b (OQ#1): the type-directed deserializer spelling
// `Type.from_json(str)`, symmetric with the value-directed `to_json(value)`.
// Both dispatch through the compiler (D15: one canonical spelling); neither is
// a hand-written mangled symbol. The old bare `Type_from_json(str)` still
// resolves the same generated extern (backward compatible).

@json
struct Point { int x; int y; }

fn main() -> int {
	Point p = Point { x: 3, y: 4 };
	string j = to_json(p);
	println("json={}", j);

	// Type-directed round-trip: Point.from_json parses back to a Point.
	Point q = Point.from_json(j);
	println("round={},{}", q.x, q.y);

	// Backward-compatible bare spelling resolves the same symbol.
	Point r = Point_from_json(j);
	println("bare={},{}", r.x, r.y);

	return 0;
}
