// U5 (format 4): a PLAIN `pub struct` whose field names a non-`pub` type is now
// LEGAL — this fixture flipped from `fail/sema` to `pass` (converted, not
// deleted, to preserve the coverage).
//
// Under U3 this was the design record's P9 reproduction: the emitter shipped
// field layout for every `pub struct`, so `Secret` genuinely crossed the
// boundary and had to be exported, and a non-`pub` field type produced an
// unreadable `.bmod` at the consumer. U5 DROPS field layout for a
// non-data-contract struct — a plain `pub struct`'s fields are no longer emitted
// into the `.bmod` (the consumer constructs through the library factory and
// calls `pub` methods), so a private field's TYPE never crosses, and requiring
// it to be `pub` would now be a false positive. The P9 field-type rule therefore
// narrows to `table`/`@json` data-contract structs, whose field shape genuinely
// crosses (D15) — see the still-failing `@json` variant
// `fail/sema/p9_pub_struct_private_field.b`, which keeps the reproduction alive
// for exactly the case where it still applies.
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
