// ARC ledger #7: a payload-carrying enum RVALUE passed directly as a call
// argument (construct or call result) owns its refcounted payload with no
// releasing owner — previously the payload leaked. The caller now registers
// the temp for scope-exit payload release; the callee borrows.
struct Counter {
	int n;
}

fn pick(Option<string> o) -> string {
	return match o {
		some(v) { v }
		none { "fallback" }
	};
}

fn make(int i) -> Option<string> {
	if i > 0 {
		return Option.some("made {i}");
	}
	return Option.none;
}

fn msg_of(Result<int, string> r) -> string {
	return match r {
		ok(v) { "ok {v}" }
		err(e) { e }
	};
}

impl Counter {
	fn take(self, Option<string> o) -> int {
		return match o {
			some(v) { v.length }
			none { 0 }
		};
	}
}

fn main() -> int {
	// Construct rvalue passed directly (the original repro).
	println("{}", pick(Option.some("hi")));
	println("{}", pick(Option.none));

	// Call-result enum rvalue passed directly.
	println("{}", pick(make(3)));
	println("{}", pick(make(0)));

	// Result with a string err payload, both variants.
	println("{}", msg_of(Result.ok(7)));
	println("{}", msg_of(Result.err("boom")));

	// Method-call argument.
	Counter c = Counter { n: 0 };
	println("{}", c.take(Option.some("abcd")));
	println("{}", c.take(Option.none));

	// Repeated in a loop: each iteration's temp released at body scope exit.
	int total = 0;
	for i in 0..10 {
		total = total + pick(Option.some("k{i}")).length;
	}
	println("{}", total);
	return 0;
}
