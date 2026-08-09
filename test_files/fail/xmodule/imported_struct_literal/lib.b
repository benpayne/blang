// With every field private, a struct literal is module-private automatically:
// the one external construction form is `Counter(...)` via `pub init` (D9/D7).
pub struct Counter {
	int count;
}

impl Counter {
	pub init(int start) {
		self.count = start;
	}

	pub fn bump(self) -> int {
		self.count = self.count + 1;
		return self.count;
	}
}
