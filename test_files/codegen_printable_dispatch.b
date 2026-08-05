// Regression: Printable dispatch through print/println passed the WRONG self.
//
// Two distinct wrong answers have lived at this site:
//   1. the receiver was boxed in a fresh alloca and the ALLOCA passed, so
//      to_string read its fields out of a stack slot and printed garbage;
//   2. the generated VALUE was passed, which is correct only for unqualified
//      locals — genVariableExpression double-loads `shared`/`sync` variables, so
//      a struct's first 8 bytes were reinterpreted as a pointer and the program
//      SEGFAULTED.
//
// The receiver pointer must come from the variable's ADDRESS (a single load),
// which is what genMethodCall does. This fixture therefore covers all three
// ownership forms and a struct wider than one pointer, so a layout-dependent
// misread cannot hide behind a lucky first field.
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

// Wider than 8 bytes: 3 fields including a refcounted one. Reading the first 8
// bytes as a pointer cannot accidentally look right here.
struct Wide {
	int a;
	string label;
	int b;
}

impl Wide {
	init(int p, string l, int q) {
		self.a = p;
		self.label = l;
		self.b = q;
	}
}

impl Printable for Wide {
	fn to_string(self) -> string {
		int p = self.a;
		int q = self.b;
		string l = self.label;
		return "Wide({p},{l},{q})";
	}
}

fn main() -> int {
	// 1. Unqualified local: direct call and dispatch must agree.
	Point p = Point(3, 4);
	println("direct = {}", p.to_string());
	println("dispatch = {}", p);

	// 2. A struct wider than a pointer, with a refcounted field.
	Wide w = Wide(1, "mid", 2);
	println("direct = {}", w.to_string());
	println("dispatch = {}", w);

	// 3. shared receiver — the alloca holds a heap pointer and
	//    genVariableExpression double-loads it.
	shared Point sp = Point(10, 20);
	println("shared = {}", sp);

	// 4. sync receiver — same shape, plus lock/unlock on reads.
	sync Point yp = Point(30, 40);
	println("sync = {}", yp);

	// 5. shared, wider than a pointer.
	shared Wide sw = Wide(5, "shared", 6);
	println("shared wide = {}", sw);

	// 6. Several Printable args in one call, with a non-struct between them.
	println("{} / {} / {}", p, 42, w);
	return 0;
}
