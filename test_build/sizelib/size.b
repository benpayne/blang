// U2 fixture: a conformance record naming a USER-DEFINED protocol.
//
// Every other fixture conforms only to `Printable`, which is a builtin
// pre-registered in every scope — so it resolved no matter where the record
// appeared in the .bmod. A user-defined protocol does not, and emitting the
// struct (with its record) before the protocol made the record a forward
// reference that the consumer's parser rejected:
//
//     sizelib.bmod:11:23: error: Unknown protocol 'Sizeable' in impl block
//
// i.e. an interface no consumer could read — the same defect class as the
// `table pub struct` break. Protocols are now emitted first.
pub protocol Sizeable {
	fn size(self) -> int;
}

pub struct Box {
	int n;
}

impl Box {
	init(int v) {
		self.n = v;
	}
}

impl Sizeable for Box {
	fn size(self) -> int {
		return self.n;
	}
}

// A non-`pub` protocol must NOT produce a record: it is not emitted into the
// interface, so a record naming it would dangle and take the file down with it.
// (Rejecting this combination at the LIBRARY build is P9 enforcement — U3's.)
protocol Hidden {
	fn secret(self) -> int;
}

impl Hidden for Box {
	fn secret(self) -> int {
		return 0;
	}
}
