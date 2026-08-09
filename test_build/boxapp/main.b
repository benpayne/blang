// modules-v2-graph U1 done-condition 1: one binary links two libraries that each
// export a same-named generic Box<T>, each instantiating Box<int> internally.
// The binary uses them via non-generic pub fns (use-capability; never names Box).
// (Calls are unqualified: .bmod dep symbols are flat-merged in the current model;
// qualified module.name access for .bmod deps is U6. The distinct fn names never
// collide; the two Box structs do collide in the flat merge but the binary never
// names Box, so that is irrelevant here — the point is the two libraries' Box<int>
// SYMBOLS stay distinct at link, which nm verifies.)
import boxa;
import boxb;

fn main() -> int {
	println("{}", boxed_a(5));
	println("{}", boxed_b(7));
	println("{}", boxed_twice(42));
	return 0;
}
