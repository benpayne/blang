// E2E test: for-in loop with range expression
// Tests: for x in 0..N range iteration

extern fn printf(cstring fmt, ...) -> int;

fn main() -> int {
	int sum = 0;

	// Range-based for-in loop: sum 0..5 = 0+1+2+3+4 = 10
	for i in 0..5 {
		sum = sum + i;
	}

	assert sum == 10, "sum of 0..5 should be 10";

	// Another range loop
	int count = 0;
	for j in 0..100 {
		count = count + 1;
	}
	assert count == 100, "count should be 100";

	printf("For-in codegen test passed!\n");
	return 0;
}
