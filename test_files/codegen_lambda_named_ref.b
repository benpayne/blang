// Test: passing named function as callback value
extern fn printf(cstring fmt, ...) -> int;

fn double_it(int x) -> int {
	return x * 2;
}

fn triple_it(int x) -> int {
	return x * 3;
}

fn apply(fn(int) -> int f, int x) -> int {
	return f(x);
}

fn main() -> int {
	// Pass named function as callback
	int r1 = apply(double_it, 5);
	// r1 should be 10

	if r1 != 10 {
		return 1;
	}

	int r2 = apply(triple_it, 4);
	// r2 should be 12

	if r2 != 12 {
		return 2;
	}

	// Store named function in fn-typed variable
	fn(int) -> int func = double_it;
	int r3 = func(7);
	// r3 should be 14

	if r3 != 14 {
		return 3;
	}

	return 0;
}
