fn apply(fn(int) -> int f, int x) -> int {
	return f(x);
}

fn main() -> int {
	fn(int) -> int doubler = fn(int x) -> int { return x * 2; };
	int result = apply(doubler, 5);
	return 0;
}
