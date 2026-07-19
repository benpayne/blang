// Operator matrix (functional-hardening U2 / REQ-002): && / || short-circuit
// evaluation WITH side-effect ordering. `touch(n)` prints "touch n" and returns
// true, so the golden proves exactly which RHS operands were evaluated. This is
// the teeth test for the short-circuit fix: pre-fix ALL four touch lines print;
// post-fix only touch 2 and touch 4 (the cases whose RHS must run) appear.

fn touch( int n ) -> bool {
	println("touch {}", n);
	return true;
}

fn main() -> int {
	// false && RHS  -> RHS must NOT run; result false.
	bool r1 = false && touch( 1 );
	assert r1 == false, "false && _ is false";

	// true && RHS   -> RHS MUST run; result = RHS = true.
	bool r2 = true && touch( 2 );
	assert r2 == true, "true && true is true";

	// true || RHS   -> RHS must NOT run; result true.
	bool r3 = true || touch( 3 );
	assert r3 == true, "true || _ is true";

	// false || RHS  -> RHS MUST run; result = RHS = true.
	bool r4 = false || touch( 4 );
	assert r4 == true, "false || true is true";

	println("results {} {} {} {}", r1, r2, r3, r4);
	println("PASS");
	return 0;
}
