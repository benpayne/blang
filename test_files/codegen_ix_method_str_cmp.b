// Interaction matrix (functional-hardening U3 / REQ-003): a string-returning
// method call used DIRECTLY as an operand — in == / != comparisons and in string
// interpolation (B3 fix). Pre-fix, `x.get() == "hi"` was false (non-string
// comparison of a user method's string result); post-fix it routes to string
// equality. Printed AND asserted.

struct Name { string first; string last; }
impl Name {
	fn full(self) -> string { return self.first.concat(" ").concat(self.last); }
	fn greeting(self) -> string { return "hi"; }
}

fn main() -> int {
	Name n = Name { first: "Ada", last: "Byte" };

	// Direct method result in == and != (B3).
	println("eq {}", n.greeting() == "hi");
	println("ne {}", n.greeting() != "bye");
	assert n.greeting() == "hi", "method == string";
	assert n.greeting() != "bye", "method != string";
	assert !( n.greeting() == "bye" ), "method == wrong is false";

	// Method result compared to another method result / built value.
	println("full [{}]", n.full());
	assert n.full() == "Ada Byte", "concat method ==";

	// Method result directly inside string interpolation.
	println("interp {} / {}", n.greeting(), n.full());

	// Method result used to drive control flow.
	if n.greeting() == "hi" {
		println("branch taken");
	} else {
		println("branch NOT taken");
	}

	println("PASS");
	return 0;
}
