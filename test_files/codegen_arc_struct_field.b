// ARC matrix (functional-hardening U1 / REQ-001): a refcounted (heap) struct
// stored into another struct's field from existing owners (must retain so the
// sources stay valid), read through in value contexts, then the whole aggregate
// dropped (each inner struct released exactly once). Runs under --leak-check.
struct Point { int x; int y; }
struct Line  { Point start; Point end; }

fn main() -> int {
	Point a = Point { x: 1, y: 2 };
	Point b = Point { x: 3, y: 4 };
	Line l = Line { start: a, end: b };

	println("start {} {}", l.start.x, l.start.y);
	println("end {} {}", l.end.x, l.end.y);
	// Read the same nested fields again (guards a stale read).
	println("start2 {} {}", l.start.x, l.start.y);
	assert l.start.x == 1, "start x";
	assert l.end.y == 4, "end y";

	// Sources were retained on store, so they remain valid.
	println("a {} {}", a.x, a.y);
	println("b {} {}", b.x, b.y);

	println("PASS");
	return 0;
}
