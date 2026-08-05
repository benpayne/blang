// P9 surface 4: an exported enum's variant payload (D17 — variants and payloads
// ARE the enum's API, and the .bmod ships them).
struct Secret {
	int n;
}

pub enum Outcome {
	ok(Secret),
	failed
}

fn main() -> int {
	return 0;
}
