// A method without `pub` is module-private (D9): it is not emitted into the
// interface, so another module cannot reach it.
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

	// No `pub` — private to this module.
	fn reset(self) {
		self.n = 0;
	}
}
