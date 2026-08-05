// BLOCKER-2: an exported conformance record whose backing method is not `pub`.
//
// The record says "Box conforms to Sizeable", so a consumer believes it can call
// `size()`. But the `pub` filter removes a non-`pub` method from the interface,
// so the library builds green and ships a .bmod whose promise the consumer
// cannot use. Rejected at the LIBRARY build (sixth P9 surface).
pub protocol Sizeable {
	fn size(self) -> int;
}

pub struct Box {
	int n;
}

impl Sizeable for Box {
	fn size(self) -> int {
		return self.n;
	}
}

fn main() -> int {
	return 0;
}
