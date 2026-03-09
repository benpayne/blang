// Test: passing lambda to function as callback
extern fn printf(cstring fmt, ...) -> int;

fn apply(fn(int) -> int f, int x) -> int {
	return f(x);
}

fn apply_twice(fn(int) -> int f, int x) -> int {
	int first = f(x);
	return f(first);
}

fn main() -> int {
	// Pass lambda to function
	int r1 = apply(fn(int x) -> int { return x * 3; }, 4);
	// r1 should be 12

	if r1 != 12 {
		return 1;
	}

	// Pass lambda with capture to function
	int factor = 5;
	int r2 = apply(fn(int x) -> int { return x * factor; }, 7);
	// r2 should be 35

	if r2 != 35 {
		return 2;
	}

	// Apply twice: double(double(3)) = 12
	int r3 = apply_twice(fn(int x) -> int { return x * 2; }, 3);
	// r3 should be 12

	if r3 != 12 {
		return 3;
	}

	return 0;
}
