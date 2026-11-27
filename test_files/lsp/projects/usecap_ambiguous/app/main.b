// modules-v2-lsp U4 (T027) fixture: a value held by use-capability whose type
// NAME is exported by two imported modules. Navigation must not guess between
// pa's Box and pb's Box: definition/hover on `x.get()` answer null.
import pa;
import pb;

fn main() -> int {
	var x = pa.make(1);
	println("{}", x.get());
	println("{}", pb.make(2).get());
	return 0;
}
