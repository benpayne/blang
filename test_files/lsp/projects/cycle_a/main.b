// modules-v2-lsp U3 negative fixture: cycle_a and cycle_b depend on each other.
// One "circular dependency" diagnostic, then the unrelated real error.
import cycle_b;

pub fn a_value() -> int {
	int x = "s";
	return cycle_b.b_value() + x;
}
