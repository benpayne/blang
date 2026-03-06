// Demo 7: Producer-Consumer with Spawn and Wait
// Features: spawn returning Task, wait, wait_all, sync variables

extern fn printf(cstring fmt, ...) -> int;

fn main() -> int {
	printf("=== Producer-Consumer with Spawn and Wait ===\n\n");

	// --- Part 1: wait on individual tasks ---
	printf("Part 1: Individual task waiting\n");
	sync int counter = 0;

	Task t1 = spawn { counter += 10; };
	Task t2 = spawn { counter += 20; };
	Task t3 = spawn { counter += 30; };

	// Wait for each task — after this, counter is guaranteed to be 60
	wait t1;
	wait t2;
	wait t3;

	printf("  counter after 3 tasks: %d (expected 60)\n", counter);
	assert counter == 60, "counter should be 60";

	// --- Part 2: wait_all for a batch of fire-and-forget spawns ---
	printf("\nPart 2: wait_all for batch processing\n");
	sync int total = 0;

	// Spawn 10 producers, each adding their index
	printf("  Spawning 10 producers...\n");
	spawn { total += 1; }
	spawn { total += 2; }
	spawn { total += 3; }
	spawn { total += 4; }
	spawn { total += 5; }
	spawn { total += 6; }
	spawn { total += 7; }
	spawn { total += 8; }
	spawn { total += 9; }
	spawn { total += 10; }

	// Wait for all to complete
	wait_all;

	printf("  total after 10 producers: %d (expected 55)\n", total);
	assert total == 55, "total should be 1+2+...+10 = 55";

	// --- Part 3: spawn more after wait_all (pool stays alive) ---
	printf("\nPart 3: Spawning after wait_all\n");

	Task t4 = spawn { total += 100; };
	wait t4;

	printf("  total after additional spawn: %d (expected 155)\n", total);
	assert total == 155, "total should be 155 after adding 100";

	printf("\nProducer-consumer demo passed!\n");
	return 0;
}
