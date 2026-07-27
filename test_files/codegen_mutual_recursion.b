// End-to-end: mutually recursive functions (is_even <-> is_odd) and a caller
// (main) defined before its callees. Verifies forward-reference resolution and
// correct runtime behavior.

fn main() -> int {
	// is_even/is_odd defined below; classify defined below too.
	if !is_even(10) { return 1; }
	if is_even(7) { return 2; }
	if classify(6) != 2 { return 3; }
	if classify(5) != 1 { return 4; }
	return 0;
}

fn classify(int n) -> int {
	if is_even(n) { return 2; }
	return 1;
}

fn is_even(int n) -> bool {
	if n == 0 { return true; }
	return is_odd(n - 1);
}

fn is_odd(int n) -> bool {
	if n == 0 { return false; }
	return is_even(n - 1);
}
