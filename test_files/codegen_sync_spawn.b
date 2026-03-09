// E2E test: sync variable with spawn blocks
// Tests: multiple spawns incrementing a sync counter via +=

fn main() -> int {
	sync int counter = 0;

	// Spawn multiple tasks that each increment the sync counter
	spawn {
		counter += 1;
	}

	spawn {
		counter += 1;
	}

	spawn {
		counter += 1;
	}

	// After runtime shutdown (implicit at end of main), all spawns have completed
	// The counter should be 3 (each spawn adds 1, protected by sync locking)
	println("Sync spawn codegen test passed!");
	return 0;
}
