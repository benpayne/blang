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
	pub init(int v) {
		self.n = v;
	}
}

impl Sizeable for Box {
	pub fn size(self) -> int {
		return self.n;
	}
}

// A non-`pub` method on an exported struct. It must NOT appear in the interface
// — that is the F-1 negative leg this fixture carries.
//
// This block used to be `impl Hidden for Box` with a non-`pub` protocol, to
// exercise the emitter's dangling-record SKIP. U3's P9 enforcement now REJECTS
// that combination at the library build, which makes the skip unreachable from
// valid source — the better outcome, and why the skip stays only as
// defence-in-depth. The rejection itself is covered by
// test_files/fail/sema/p9_private_protocol_conformance.b.
impl Box {
	fn secret(self) -> int {
		return 0;
	}
}

// A second user-defined protocol whose conformance IS exported, with its backing
// method correctly `pub`. The rejected form — an exported conformance whose
// method is not `pub`, which would ship a .bmod promising a conformance the
// consumer cannot call — is covered by
// test_files/fail/sema/p9_conformance_method_not_pub.b.
pub protocol Labelled {
	fn label(self) -> string;
}

impl Labelled for Box {
	pub fn label(self) -> string {
		return "box";
	}
}
