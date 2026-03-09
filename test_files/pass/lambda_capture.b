fn main() -> int {
	int offset = 10;
	fn(int) -> int adder = fn(int x) -> int { return x + offset; };
	int result = adder(5);
	return 0;
}
