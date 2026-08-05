// P9 surface 5 (MINOR-1): a non-`pub` protocol named in an exported conformance
// record. The emitter SKIPS such a record rather than emitting a dangling
// reference that would make the interface unparseable; this is where the
// combination is actually rejected.
protocol Hidden {
	fn secret(self) -> int;
}

pub struct Box {
	int n;
}

impl Hidden for Box {
	fn secret(self) -> int {
		return self.n;
	}
}

fn main() -> int {
	return 0;
}
