// bcc test fixture: every test passes.
//
// Includes a test literally named `add_two` so that
// `bcc test --filter add_two` selects a strict, non-empty subset (1 of 4).
// The other three test names do NOT contain the substring "add_two", so the
// filtered passed-count (1) is strictly less than the unfiltered count (4).

fn add(int a, int b) -> int {
	return a + b;
}

fn mul(int a, int b) -> int {
	return a * b;
}

fn sub(int a, int b) -> int {
	return a - b;
}

test "add_two" {
	assert add(2, 3) == 5;
}

test "multiply" {
	assert mul(4, 5) == 20;
}

test "subtract" {
	assert sub(10, 4) == 6;
}

test "is_even" {
	assert mul(2, 3) % 2 == 0;
}
