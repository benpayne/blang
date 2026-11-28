// modules-v2-lsp U4 (T041a, F2) fixture: a library whose DIRECTORY and
// blang.toml name are `shapes` but whose only SOURCE FILE is geometry.b, so a
// hover golden can tell the two candidate human names apart.
pub struct Sq {
	int side;
}

impl Sq {
	pub init(int s) {
		self.side = s;
	}

	pub fn area(self) -> int {
		return self.side * self.side;
	}
}
