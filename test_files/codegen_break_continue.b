// E2E test: break and continue statements in loops
// Tests: break exits loop, continue skips to next iteration

fn main() -> int {
	// Test break in while loop
	int i = 0;
	int sum = 0;
	while i < 100 {
		if i == 5 {
			break;
		}
		sum = sum + i;
		i = i + 1;
	}
	assert sum == 10, "break: sum of 0..5 should be 10";

	// Test continue in for-in loop: skip even numbers
	int odd_sum = 0;
	for j in 0..10 {
		int rem = j % 2;
		if rem == 0 {
			continue;
		}
		odd_sum = odd_sum + j;
	}
	assert odd_sum == 25, "continue: sum of odd 0..10 should be 25";

	// Test break in for-in loop
	int count = 0;
	for k in 0..1000 {
		if k == 3 {
			break;
		}
		count = count + 1;
	}
	assert count == 3, "break in for-in: should count 3";

	// Test break in infinite loop
	int inf_count = 0;
	for {
		if inf_count == 7 {
			break;
		}
		inf_count = inf_count + 1;
	}
	assert inf_count == 7, "break in infinite loop: should be 7";

	println("Break/continue codegen test passed!");
	return 0;
}
