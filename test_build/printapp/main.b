import printlib;

fn main() -> int {
	Point p = Point(3, 4);

	// Printable dispatch on an IMPORTED type: only works because the .bmod
	// carries the conformance record (D16).
	println("{}", p);
	println("sum = {}", p.sum());
	return 0;
}
