// THE design record's P9 reproduction, in its plain form — no annotation.
//
// A `pub struct` whose field names a non-`pub` type emitted a .bmod referencing
// `Secret` without declaring it. The library built GREEN, exit 0, no warning,
// and the failure landed on the CONSUMER as a syntax error inside a generated
// file they never wrote:
//
//     leaky.bmod:5:8: error: Expected field type in struct definition
//
// followed by a cascade blaming their own correct code.
//
// This fixture carries the reproduction because it has NO annotation: field
// layout ships for every `pub struct` today, so every `pub struct`'s field types
// cross the boundary — not just `table`/`@json` ones. (Manager ruling
// 2026-08-05, spec 031 §4.4 item 3. When U5 drops layout, only annotated structs
// keep field metadata and the narrower rule becomes correct.)
struct Secret {
	int hidden;
}

pub struct Leaky {
	int id;
	Secret payload;
}

fn main() -> int {
	return 0;
}
