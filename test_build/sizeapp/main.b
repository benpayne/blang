import sizelib;

fn main() -> int {
	Box b = Box(11);
	println("size = {}", b.size());
	// A second user-defined protocol crossing the boundary, with a `pub`
	// conformance method (F-1: a USER-DEFINED instance, not a builtin).
	println("label = {}", b.label());
	// INTENDED BREAKAGE, landed in U3 (known-issues KI-4, flagged in place by U2):
	// `secret` is reachable only through `impl Hidden for Box`, and `Hidden` is not
	// `pub`. Under U2's interim semantics every non-generic method shipped in the
	// .bmod, so this call compiled. U3's `pub` filter removes it from the
	// interface — correctly — so the call is gone.
	//
	// `secret` staying private is deliberate: sizelib/sizeapp is the F-1 fixture
	// pair whose NEGATIVE leg asserts a non-`pub` method is absent from the .bmod.
	return 0;
}
