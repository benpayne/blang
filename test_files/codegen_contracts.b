// End-to-end codegen test for requires/ensures contracts

fn divide(int a, int b) -> int requires b != 0 {
	return a;
}

fn positive(int x) -> int ensures result >= 0 {
	if (x < 0)
		return 0;
	return x;
}

fn safeDivide(int a, int b) -> int requires b != 0 ensures result >= 0 {
	return a;
}

fn main() -> int {
	int r1 = divide(10, 2);
	int r2 = positive(5);
	int r3 = positive(-3);
	int r4 = safeDivide(10, 2);
	return 0;
}
