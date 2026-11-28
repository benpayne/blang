// modules-v2-lsp U4 (T027) fixture: two libraries each export a struct named
// Box (D4: legal — qualified access keeps them apart).
pub struct Box {
	int v;
}

impl Box {
	pub init(int x) {
		self.v = x;
	}

	pub fn get(self) -> int {
		return self.v;
	}
}

pub fn make(int x) -> Box {
	return Box(x);
}
