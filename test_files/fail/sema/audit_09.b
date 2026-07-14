// audit_09 (design.md "The 10 audit programs"): non-exhaustive match — a match on
// an enum that omits a variant with no wildcard arm. Today the missing variant is
// silently unhandled; U5 rejects it at the semantic stage.
enum Color { red, green, blue }

fn pick(Color c) -> int {
	match c {
		red { return 1; }
		green { return 2; }
	}
	return 0;
}

fn main() -> int { return 0; }
