// E2E test: wait for a single spawned task
// Tests: Task t = spawn { }; wait t;

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
	println("counter = {}", counter);
	assert counter == 30, "counter should be 30 after waiting";

	println("Wait codegen test passed!");
	return 0;
}
