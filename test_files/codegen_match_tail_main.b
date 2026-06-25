// Regression: a match as the final statement of main() (with no explicit
// return after it) must leave the match merge block terminated by the
// implicit return. The merge block is created before the arm blocks, so the
// implicit-return logic must target the real end-of-body block, not the
// physically-last block in the function.

enum Signal {
	red,
	green,
	blue
}

fn main() -> int {
	Signal s = Signal.green;
	match s {
		red {
		}
		green {
		}
		blue {
		}
	}
}
