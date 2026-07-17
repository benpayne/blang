// Codegen test: a match that explicitly covers every enum variant (no
// wildcard) passes exhaustiveness checking and dispatches correctly.

enum Signal {
	red,
	yellow,
	green
}

fn main() -> int {
	Signal s = Signal.yellow;
	match s {
		red {
			return 1;
		}
		yellow {
			return 0;
		}
		green {
			return 2;
		}
	}
	return 3;
}
