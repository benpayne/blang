// Function with ensures postcondition

fn absolute(int x) -> int ensures result >= 0 {
	if (x < 0)
		return 0;
	return x;
}

fn main() -> int {
	return 0;
}
