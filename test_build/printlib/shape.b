// U2 fixture: protocol conformance across a .bmod boundary (design record D16).
//
// `impl Printable for Point` used to be checked at the library build and then
// discarded — the .bmod carried no record of it, so a consumer could not
// dispatch `print("{}", p)` through Printable. The interface now ships the
// conformance record.
//
// This is also a `table` struct, so the same fixture covers the M1 emission
// order break: the .bmod used to say `table pub struct`, which is a hard parse
// error (`Expected 'struct' after 'table'`) — an interface no consumer could
// read at all.
pub table struct Point {
	int x;
	int y;
}

impl Point {
	pub init(int px, int py) {
		self.x = px;
		self.y = py;
	}

	pub fn sum(self) -> int {
		return self.x + self.y;
	}
}

impl Printable for Point {
	pub fn to_string(self) -> string {
		// NOTE: the fields are copied into locals deliberately. A dotted
		// expression inside a string interpolation ("{self.x}") is silently
		// emitted as literal text rather than interpolated — a pre-existing
		// codegen bug unrelated to this epic, filed as known-issues KI-8.
		int px = self.x;
		int py = self.y;
		return "Point({px}, {py})";
	}
}
