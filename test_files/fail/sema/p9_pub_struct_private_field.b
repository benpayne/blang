// The design record's P9 reproduction. A `pub struct` whose field names a
// non-`pub` type used to emit a .bmod referencing `Secret` without declaring it:
// the library built GREEN, exit 0, no warning, and the failure landed on the
// CONSUMER as a syntax error inside a generated file they never wrote
// (`leaky.bmod:5:8: error: Expected field type in struct definition`), followed
// by a cascade blaming their own correct code.
//
// It is a data-contract struct (@json), so its field types genuinely cross the
// boundary (D15) — which is exactly when the reference must be exported.
struct Secret {
	int hidden;
}

@json
pub struct Leaky {
	int id;
	Secret payload;
}

fn main() -> int {
	return 0;
}
