// Function with both requires and ensures

fn safeDivide(int a, int b) -> int requires b != 0 ensures result >= 0 {
	return a;
}

fn main() -> int {
	return 0;
}
