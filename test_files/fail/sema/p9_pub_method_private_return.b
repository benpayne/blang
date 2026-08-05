// P9 surface 2: an exported method's return type.
struct Secret {
	int n;
}

pub struct Holder {
	int n;
}

impl Holder {
	pub init(int v) { self.n = v; }

	pub fn leak(self) -> Secret {
		return Secret { n: self.n };
	}
}

fn main() -> int {
	return 0;
}
