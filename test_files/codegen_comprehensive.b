// Comprehensive E2E test: all Phase 2 features together
// Tests: assert, contracts, ownership, spawn, for-in, async/await stubs

extern fn printf(cstring fmt, ...) -> int;

fn abs(int x) -> int requires x >= -1000 ensures result >= 0 {
	if x < 0 {
		return 0 - x;
	}
	return x;
}

fn add(int a, int b) -> int {
	return a + b;
}

fn main() -> int {
	// 1. Assert statements
	assert 1 == 1;
	assert 42 > 0, "42 should be positive";

	// 2. Variable declarations with ownership
	own int x = 5;
	int y = 10;

	// 3. Arithmetic and assignment
	int sum = add(x, y);
	assert sum == 15, "5 + 10 should be 15";

	// 4. Contract-checked function
	int a = abs(-7);
	assert a == 7, "abs(-7) should be 7";

	int b = abs(3);
	assert b == 3, "abs(3) should be 3";

	// 5. For-in range loop
	int total = 0;
	for i in 0..10 {
		total = total + i;
	}
	assert total == 45, "sum of 0..9 should be 45";

	// 6. Spawn block (runs on thread pool)
	spawn {
		int spawned = 1;
	}

	// 7. Nested control flow
	int result = 0;
	for k in 0..5 {
		if k > 2 {
			result = result + k;
		}
	}
	assert result == 7, "3 + 4 should be 7";

	printf("Comprehensive Phase 2 test passed!\n");
	return 0;
}
