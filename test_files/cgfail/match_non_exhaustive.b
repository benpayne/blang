// Non-exhaustive match on an enum: the 'none' variant is not handled and
// there is no wildcard '_' arm, so codegen must reject this.

enum Option {
	some(int),
	none
}

fn main() -> int {
	Option a = Option.some(42);
	match a {
		some(val) {
			return val;
		}
	}
	return 0;
}
