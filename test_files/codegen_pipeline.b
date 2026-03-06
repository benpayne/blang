// E2E test: pipeline operator |>
// Tests: expr |> fn(args) desugars to fn(expr, args)

extern fn printf(cstring fmt, ...) -> int;

fn add(int a, int b) -> int {
	return a + b;
}

fn double_it(int x) -> int {
	return x * 2;
}

fn main() -> int {
	// Basic pipeline: 5 |> add(3) == add(5, 3) == 8
	int result = 5 |> add(3);
	assert result == 8, "pipeline: 5 |> add(3) should be 8";

	// Chained pipeline: 5 |> add(3) |> double_it() == double_it(add(5, 3)) == 16
	int chained = 5 |> add(3) |> double_it();
	assert chained == 16, "pipeline: 5 |> add(3) |> double_it() should be 16";

	// Pipeline with variable input
	int x = 10;
	int piped = x |> add(20);
	assert piped == 30, "pipeline: x |> add(20) should be 30";

	printf("Pipeline codegen test passed!\n");
	return 0;
}
