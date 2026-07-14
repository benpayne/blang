// U6: reassigning an own variable after it was moved clears the moved state, so a
// subsequent use is valid (accepted-program test for move analysis).
fn main() -> int {
	own string a = "hello";
	own string b = a;
	a = "world";
	own string c = a;
	return 0;
}
