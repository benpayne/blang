// modules-v2-lsp U3 negative fixture: the dependency's own compile fails. One
// "failed to compile" diagnostic at the import naming the dependency's first
// error, no cascade for 'lib', and the unrelated real error below.
import lib;

fn main() -> int {
	int v = lib.value();
	int x = "s";
	return v + x;
}
