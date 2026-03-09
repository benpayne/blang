// E2E test: wait_all for all spawned tasks
// Tests: spawn multiple tasks, wait_all, verify all completed

fn main() -> int {
	sync int total = 0;

	spawn { total += 1; }
	spawn { total += 2; }
	spawn { total += 3; }
	spawn { total += 4; }
	spawn { total += 5; }

	// Wait for all spawned tasks to complete
	wait_all;

	// total should be 1+2+3+4+5 = 15
	println("total = {}", total);
	assert total == 15, "total should be 15 after wait_all";

	// Spawn more work after wait_all (pool is still alive)
	spawn { total += 10; }
	wait_all;

	println("total after second batch = {}", total);
	assert total == 25, "total should be 25 after second batch";

	println("Wait_all codegen test passed!");
	return 0;
}
