// modules-v2-graph U1 done-condition 1: one binary links two libraries that each
// export a same-named generic Box<T>, each instantiating Box<int> internally.
// The binary uses them via non-generic pub fns (use-capability; never names Box).
// modules-v2-graph U6a: a dependency's free functions are reached QUALIFIED
// (module.name) after `import module;`. The two libraries' distinct fn names —
// now qualified by their module — never collide; the two Box structs still never
// need naming here (the binary only holds/prints their return values), so the
// point stands: the two libraries' Box<int> SYMBOLS stay distinct at link, which
// nm verifies.
import boxa;
import boxb;

fn main() -> int {
	println("{}", boxa.boxed_a(5));
	println("{}", boxb.boxed_b(7));
	println("{}", boxa.boxed_twice(42));
	return 0;
}
