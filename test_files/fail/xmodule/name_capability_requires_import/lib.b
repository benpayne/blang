// modules-v2-graph U6b — D7 name-capability. This module exports a struct. A
// consumer may NAME the type (declare a variable of it, construct one) only after
// `import lib;` — that import is what grants name-capability.
pub struct Widget {
	int size;
}

impl Widget {
	pub init(int s) { self.size = s; }
	pub fn size_of(self) -> int { return self.size; }
}
