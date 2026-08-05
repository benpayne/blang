// U3 step 1: `pub` parses on every impl-member form. Visibility itself is
// private-by-default (D9) — an unmarked member is module-visible only — but
// enforcement and the .bmod filter land in later steps of this unit. This
// fixture pins the SYNTAX.
pub struct Counter {
	int n;
}

impl Counter {
	pub init(int v) {
		self.n = v;
	}

	pub fn bump(self) -> int {
		self.n = self.n + 1;
		return self.n;
	}

	// No `pub` — private to this module, and callable from within it.
	fn reset(self) {
		self.n = 0;
	}

	pub static fn zero() -> int {
		return 0;
	}

	static fn one() -> int {
		return 1;
	}
}

fn main() -> int {
	Counter c = Counter(1);
	c.reset();
	return c.bump();
}
