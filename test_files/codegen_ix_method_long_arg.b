// Interaction matrix (functional-hardening U3 / REQ-003): integer arguments
// passed to a `long` method parameter (B2 fix). Pre-fix, an int-typed argument
// to a long parameter emitted a type-mismatched call that failed IR verification
// (ICE). Post-fix the argument is width-promoted at the method-call site.
// Printed AND asserted.

struct Accum { long total; }
impl Accum {
	fn add(self, long v) -> long { self.total = self.total + v; return self.total; }
	fn scale(self, long factor, int bump) -> long { return self.total * factor + bump; }
}

fn main() -> int {
	Accum a = Accum { total: 0 };

	// int LITERAL -> long parameter (the ICE case pre-fix).
	long r1 = a.add(11);
	println("r1 {}", r1);
	assert r1 == 11, "add int literal";

	// int VARIABLE -> long parameter.
	int step = 7;
	long r2 = a.add(step);
	println("r2 {}", r2);
	assert r2 == 18, "add int variable";

	// long local -> long parameter (no coercion needed).
	long big = 1000;
	long r3 = a.add(big);
	println("r3 {}", r3);
	assert r3 == 1018, "add long";

	// mixed int expression -> long parameter, plus a trailing int parameter.
	long r4 = a.scale(2, 5);      // 1018 * 2 + 5 = 2041
	println("r4 {}", r4);
	assert r4 == 2041, "scale mixed args";

	println("PASS");
	return 0;
}
