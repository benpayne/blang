fn double_it(int x) -> int {
	return x * 2;
}

fn main() -> int {
	fn(int) -> int doubler = fn(int x) -> int { return x * 2; };
	return 0;
}
