// Test block with assert and failure message

fn isPositive(int x) -> int {
	return x;
}

test "positive check" {
	int x = 5;
	assert x > 0, "x should be positive";
}
