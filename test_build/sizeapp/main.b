import sizelib;

fn main() -> int {
	Box b = Box(11);
	println("size = {}", b.size());
	// INTERIM SEMANTICS (known-issues KI-4): `secret` is reachable only through
	// `impl Hidden for Box`, and `Hidden` is NOT `pub`. U2 still ships every
	// non-generic method in the .bmod because `pub` cannot yet be written on impl
	// members, so this call compiles today.
	//
	// U3's `pub` filter MUST break this line. That change is intended, not a
	// regression: when it lands, either mark the method `pub` or delete this call
	// and its expected-output line.
	println("secret = {}", b.secret());
	return 0;
}
