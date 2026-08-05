// Regression: a method that RETURNS another method's string result freed the
// string before returning it (known-issues KI-8(b)).
//
//   fn to_string(self) -> string { return self.describe(); }
//
// compiled to:
//
//   %s = call ptr @P_describe(ptr %self)
//   call void @__blang_string_release(ptr %s)   <- refcount 1 -> 0, FREED
//   ret ptr %s                                   <- caller gets a dangling ptr
//
// Root cause, and it is the same one as KI-10: the implicit `self` parameter's
// declared type name is the literal string "self", not the enclosing struct's
// name. `methodReturnTypeName` resolves a receiver by looking its declared type
// name up in the struct map, so it missed on every self receiver and returned
// "". `isStringType` therefore said "not a string" for `self.describe()`, the
// return statement skipped its retain, and the scope's temp-string release
// freed the value being returned.
//
// It reproduced as heap corruption, not as a clean crash: the caller printed a
// freed BlangString whose length field had been reused, so the runtime aborted
// with "out of memory in string concat_many". A test that only checks the exit
// code of a program that never reads the string would miss it entirely — hence
// the reads below.
struct P {
	int n;
	string tag;
}

impl P {
	init(int a, string t) {
		self.n = a;
		self.tag = t;
	}

	fn describe(self) -> string {
		return "P({self.n},{self.tag})";
	}

	// One level of delegation: return another method's string result.
	fn delegate(self) -> string {
		return self.describe();
	}

	// Two levels: the delegation is itself delegated.
	fn delegate2(self) -> string {
		return self.delegate();
	}

	// A borrowed field, for contrast — this path always worked and must stay
	// working (it needs the retain that a fresh temp must NOT get twice).
	fn get_tag(self) -> string {
		return self.tag;
	}
}

impl Printable for P {
	// The shape from the KI-8(b) report: Printable's to_string delegates.
	fn to_string(self) -> string {
		return self.describe();
	}
}

struct Holder {
	P inner;
}

impl Holder {
	init(P p) {
		self.inner = p;
	}

	// Delegation through a FIELD receiver rather than `self`.
	fn describe(self) -> string {
		return self.inner.describe();
	}
}

// Delegation through a plain parameter receiver, outside any method.
fn via_param(P p) -> string {
	return p.describe();
}

fn main() -> int {
	P p = P(3, "hi");

	// Direct call — always worked.
	println("A={}", p.describe());

	// One and two levels of delegation. Each returned string is then READ
	// (concatenated into the print), which is what turns a use-after-free into
	// an observable wrong answer rather than a silently ignored one.
	println("B={}", p.delegate());
	println("C={}", p.delegate2());

	// Bind first, then read twice: a freed string survives one read by luck.
	string s = p.delegate();
	println("D={}", s);
	println("E={}", s);

	// Printable dispatch whose to_string delegates — both spellings must agree.
	println("F={}", p.to_string());
	println("G={}", p);
	println("H={}", "[{p}]");

	// Field receiver and parameter receiver.
	Holder h = Holder(p);
	println("I={}", h.describe());
	println("J={}", via_param(p));

	// A borrowed field return still hands the caller its own reference.
	string t = p.get_tag();
	println("K={}", t);
	println("L={}", p.get_tag());

	println("method return delegation codegen test passed!");
	return 0;
}
