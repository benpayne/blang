// Regression (known-issues KI-21): a FIELD-ACCESS struct argument in the
// direct-print form `println("{}", h.inner)` fell through to the raw-pointer
// path — genPrintCall tested only VariableExpression args for struct-ness, so a
// field access was handed to __blang_string_concat_many as if it were a
// BlangString (the KI-10 class, one argument shape not covered).
//
// Covers a field-access struct arg in BOTH the direct-print form and the
// interpolation form, read from OUTSIDE the owning struct and from INSIDE it
// (a `self.inner` receiver), for a Printable inner struct.
struct Inner {
	int n;
}

impl Inner {
	init(int v) {
		self.n = v;
	}
}

impl Printable for Inner {
	fn to_string(self) -> string {
		int v = self.n;
		return "Inner({v})";
	}
}

struct Holder {
	Inner inner;
	int tag;
}

impl Holder {
	init(int v, int t) {
		self.inner = Inner(v);
		self.tag = t;
	}

	// A field-access struct receiver read from INSIDE the owning struct, via
	// both the direct-print and the interpolation form.
	fn show(self) {
		println("self.inner direct = {}", self.inner);
		string s = "self.inner interp = {self.inner}";
		println("{}", s);
	}
}

fn main() -> int {
	Holder h = Holder(7, 99);

	// Field-access struct arg, direct-print form — the KI-21 shape.
	println("h.inner direct = {}", h.inner);

	// Field-access struct arg, interpolation form.
	string s = "h.inner interp = {h.inner}";
	println("{}", s);

	// Same reads from inside the owning struct (self-based field access).
	h.show();

	return 0;
}
