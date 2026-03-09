// Demo 7: Producer-Consumer with Spawn and Wait
// Features: spawn returning Task, wait, wait_all, sync variables

fn main() -> int {
	println("=== Producer-Consumer with Spawn and Wait ===");
	println();

	// --- Part 1: wait on individual tasks ---
	println("Part 1: Individual task waiting");
	sync int counter = 0;

	Task t1 = spawn { counter += 10; };
	Task t2 = spawn { counter += 20; };
	Task t3 = spawn { counter += 30; };

	// Wait for each task — after this, counter is guaranteed to be 60
	wait t1;
	wait t2;
	wait t3;

	println("  counter after 3 tasks: {} (expected 60)", counter);
	assert counter == 60, "counter should be 60";

	// --- Part 2: wait_all for a batch of fire-and-forget spawns ---
	println();
	println("Part 2: wait_all for batch processing");
	sync int total = 0;

	// Spawn 10 producers, each adding their index
	println("  Spawning 10 producers...");
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

	println("  total after 10 producers: {} (expected 55)", total);
	assert total == 55, "total should be 1+2+...+10 = 55";

	// --- Part 3: spawn more after wait_all (pool stays alive) ---
	println();
	println("Part 3: Spawning after wait_all");

	Task t4 = spawn { total += 100; };
	wait t4;

	println("  total after additional spawn: {} (expected 155)", total);
	assert total == 155, "total should be 155 after adding 100";

	println();
	println("Producer-consumer demo passed!");
	return 0;
}
