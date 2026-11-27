// modules-v2-lsp U3 fixture: the consumer of an open, unsaved dependency.
import lib;

fn main() -> int {
	int a = lib.helper();
	int b = lib.wrap(a);
	println("{}", a + b);
	return 0;
}
