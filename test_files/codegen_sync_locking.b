// E2E test: sync variable locking
// Tests: sync read lock/unlock, sync = with self-reference (TOCTOU fix), sync +=

extern fn printf(string fmt, ...) -> int;

fn main() -> int {
	// sync variable with locked reads and writes
	sync int counter = 0;

	// Simple sync read (should lock/unlock around read)
	int val = counter;
	assert val == 0, "initial sync read should be 0";

	// Sync = assignment with self-reference (TOCTOU fix: RHS evaluated inside lock)
	counter = counter + 1;
	val = counter;
	assert val == 1, "counter should be 1 after counter = counter + 1";

	// Sync += compound assignment (lock around read-modify-write)
	counter += 5;
	val = counter;
	assert val == 6, "counter should be 6 after += 5";

	// Self-referencing expression: counter = counter + counter
	counter = counter + counter;
	val = counter;
	assert val == 12, "counter should be 12 after counter = counter + counter";

	printf("Sync locking codegen test passed!\n");
	return 0;
}
