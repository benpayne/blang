// Regression (known-issues KI-20): a `shared`/`sync` struct used as a STRING
// INTERPOLATION part passed the wrong self pointer.
//
// The KI-8/KI-10 fix renders a struct interpolation part `"{obj}"` through
// Printable, but took the self pointer from genExpression's LOADED value. For a
// `shared`/`sync` struct that value is the struct's first 8 bytes reinterpreted
// as a pointer (genVariableExpression double-loads those qualifiers), so
// `"{sp}"` would print garbage or segfault — the same defect the sibling
// direct-print path already avoided by taking the pointer from the ADDRESS.
//
// This fixture exercises the INTERPOLATION path (not direct print, which
// codegen_printable_dispatch.b already covers) for unqualified, shared, and sync
// receivers, and a struct wider than one pointer so a layout-dependent misread
// cannot hide behind a lucky first field.
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
	// Unqualified local as an interpolation part.
	Point p = Point(3, 4);
	string s1 = "local={p}";
	println("{}", s1);

	// shared struct as an interpolation part — the KI-20 shape.
	shared Point sp = Point(10, 20);
	string s2 = "shared={sp}";
	println("{}", s2);

	// sync struct as an interpolation part.
	sync Point yp = Point(30, 40);
	string s3 = "sync={yp}";
	println("{}", s3);

	// shared, wider than a pointer, with a refcounted field.
	shared Wide sw = Wide(5, "mid", 6);
	string s4 = "wide={sw}";
	println("{}", s4);

	// Multiple struct parts and a literal in one interpolation.
	string s5 = "both={p}/{sp}";
	println("{}", s5);

	return 0;
}
