// A member variable is always private (D9). Under U5 the .bmod ships no field
// layout for a plain struct, so a consumer cannot name `count` at all.
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
