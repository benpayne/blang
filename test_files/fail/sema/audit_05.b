// audit_05: wrong number of arguments to a call.
fn f(int a) -> int { return a; }
fn main() -> int {
	return f(1, 2, 3);
}
