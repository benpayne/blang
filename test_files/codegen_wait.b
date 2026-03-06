// E2E test: wait for a single spawned task
// Tests: Task t = spawn { }; wait t;

extern fn printf(cstring fmt, ...) -> int;

fn main() -> int {
	sync int counter = 0;

	Task t1 = spawn {
		counter += 10;
	};

	Task t2 = spawn {
		counter += 20;
	};

	// Wait for each task individually
	wait t1;
	wait t2;

	// After waiting, counter should be 30
	printf("counter = %d\n", counter);
	assert counter == 30, "counter should be 30 after waiting";

	printf("Wait codegen test passed!\n");
	return 0;
}
