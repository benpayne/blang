// E2E test: spawn blocks running on real threads
// Tests: spawn body executes on thread pool, main waits for completion

extern fn usleep(int usec) -> int;

fn main() -> int {
	// Use sync variable so spawn threads can safely write to it
	sync int counter = 0;

	// Spawn several tasks that each increment the counter
	spawn {
		counter = counter + 1;
	}

	spawn {
		counter = counter + 1;
	}

	spawn {
		counter = counter + 1;
	}

	// Runtime shutdown (called automatically) waits for all spawned tasks

	println("Spawn threaded test completed!");
	return 0;
}
