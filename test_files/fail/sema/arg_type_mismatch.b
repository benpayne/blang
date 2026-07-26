// U4 diagnostic coverage: argument type mismatch (distinct from arity).
fn takes_int(int n) -> int { return n; }
fn main() -> int {
	int r = takes_int("hello");
	return r;
}
