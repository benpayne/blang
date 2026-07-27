// Forward references: main is defined before the helpers it calls, and the
// helpers are mutually recursive. Function bodies are parsed after all
// signatures are registered, so definition order does not matter.

fn main() -> int {
	if is_even(10) {
		return helper();
	}
	return 1;
}

fn helper() -> int {
	return 0;
}

fn is_even(int n) -> bool {
	if n == 0 { return true; }
	return is_odd(n - 1);
}

fn is_odd(int n) -> bool {
	if n == 0 { return false; }
	return is_even(n - 1);
}
