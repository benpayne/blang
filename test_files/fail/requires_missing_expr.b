// requires without expression should fail

fn divide(int a, int b) -> int requires {
	return a;
}

fn main() -> int {
	return 0;
}
