// P9 surface 1: an exported function's parameter type.
struct Secret {
	int n;
}

pub fn use_it(Secret s) -> int {
	return s.n;
}

fn main() -> int {
	return 0;
}
