// Fail test: dynamic (non-literal) format string

fn main() -> int {
	string fmt = "hello {}";
	int x = 42;
	println(fmt, x);
	return 0;
}
