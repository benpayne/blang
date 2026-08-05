// Regression: Printable dispatch through print/println passed the WRONG self.
//
// A struct value is a refcounted heap pointer, but the print path stored that
// pointer into a fresh alloca and passed the alloca — a pointer to the self
// pointer. to_string then read its fields out of the stack slot and printed
// garbage, silently, for every Printable struct. Calling p.to_string() directly
// was always correct, so the bug only showed through the print placeholder.
struct Point {
	int x;
	int y;
}

impl Point {
	init(int a, int b) {
		self.x = a;
		self.y = b;
	}
}

impl Printable for Point {
	fn to_string(self) -> string {
		int a = self.x;
		int b = self.y;
		return "Point({a},{b})";
	}
}

struct Named {
	string label;
}

impl Named {
	init(string n) { self.label = n; }
}

impl Printable for Named {
	fn to_string(self) -> string {
		string l = self.label;
		return "Named[{l}]";
	}
}

fn main() -> int {
	Point p = Point(3, 4);
	Named n = Named("widget");

	// Direct call and dispatch must agree.
	println("direct = {}", p.to_string());
	println("dispatch = {}", p);

	// A refcounted field through the same path.
	println("direct = {}", n.to_string());
	println("dispatch = {}", n);

	// Two Printable args in one call, plus a non-struct arg between them.
	println("{} / {} / {}", p, 42, n);
	return 0;
}
