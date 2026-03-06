// Test mixed-width comparisons and arithmetic
fn test_mixed_types() -> int {
	char c = 65;
	short s = 100;
	int i = 42;
	long l = 999999;
	bool b = true;

	// char vs int comparison
	if c != 65 { return 1; }

	// short vs int comparison
	if s != 100 { return 2; }

	// long vs int comparison
	if l != 999999 { return 3; }

	// bool vs int comparison
	if b != true { return 4; }

	// Mixed arithmetic
	int sum1 = s + i;
	int sum2 = c + i;
	long sum3 = l + i;

	return 0;
}

fn main() -> int {
	return test_mixed_types();
}
