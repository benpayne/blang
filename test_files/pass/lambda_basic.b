fn main() -> int {
	fn(int) -> int doubler = fn(int x) -> int { return x * 2; };
	int result = doubler(5);
	return 0;
}
