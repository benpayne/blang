// Test block with assert statement

fn square(int x) -> int {
	return x;
}

test "square computes correctly" {
	int result = square(4);
	assert result == 4;
}
