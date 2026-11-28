// modules-v2-lsp U3 negative fixture: a path dependency that does not exist.
// Expect ONE setup diagnostic at the import, no cascade for 'nope', and the
// unrelated real error below.
import nope;

fn main() -> int {
	int v = nope.f();
	int x = "s";
	return v + x;
}
