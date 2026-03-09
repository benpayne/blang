// Test: basic lambda, capture, and indirect call
extern fn printf(cstring fmt, ...) -> int;

fn main() -> int {
	// Basic lambda
	fn(int) -> int doubler = fn(int x) -> int { return x * 2; };
	int r1 = doubler(5);
	// r1 should be 10

	// Lambda with capture
	int offset = 100;
	fn(int) -> int adder = fn(int x) -> int { return x + offset; };
	int r2 = adder(5);
	// r2 should be 105

	if r1 != 10 {
		return 1;
	}

	if r2 != 105 {
		return 2;
	}

	return 0;
}
