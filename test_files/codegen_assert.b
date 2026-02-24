// End-to-end codegen test for assert statements

fn main() -> int {
	int x = 42;
	assert x == 42;
	assert x > 0, "x should be positive";

	int y = 10;
	assert y != 0;
	assert y <= 10;

	return 0;
}
