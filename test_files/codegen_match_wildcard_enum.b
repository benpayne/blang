// Codegen test: a wildcard '_' arm makes a partial enum match exhaustive.
// Only 'red' is handled explicitly; the wildcard covers yellow and green.

enum Signal {
	red,
	yellow,
	green
}

fn main() -> int {
	Signal s = Signal.green;
	match s {
		red {
			return 1;
		}
		_ {
			// catches yellow and green
			return 0;
		}
	}
	return 2;
}
