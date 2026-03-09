// Demo 9: Enum State Machine — Traffic Light
// Features: enums with payloads, match expressions, while loop, state transitions

// Traffic light states with duration (in seconds)
enum Light {
	red(int),
	yellow(int),
	green(int)
}

// Advance to the next state
fn next_light(Light current) -> Light {
	Light next = Light.red(30);
	match current {
		red(duration) {
			next = Light.green(25);
		}
		green(duration) {
			next = Light.yellow(5);
		}
		yellow(duration) {
			next = Light.red(30);
		}
	}
	return next;
}

// Get the name of the current light state
fn light_name(Light l) -> string {
	string name = "unknown";
	match l {
		red(d)    { name = "RED"; }
		green(d)  { name = "GREEN"; }
		yellow(d) { name = "YELLOW"; }
	}
	return name;
}

// Get duration of current state
fn light_duration(Light l) -> int {
	int dur = 0;
	match l {
		red(d)    { dur = d; }
		green(d)  { dur = d; }
		yellow(d) { dur = d; }
	}
	return dur;
}

fn main() -> int {
	println("=== Traffic Light State Machine ===");
	println();

	// Start at red
	Light state = Light.red(30);
	println("Initial state: {} ({} sec)", light_name(state), light_duration(state));
	println();

	// Run through 9 transitions (3 full cycles)
	println("State transitions:");
	int total_time = light_duration(state);
	for cycle in 0..9 {
		Light prev = state;
		state = next_light(state);
		int dur = light_duration(state);
		total_time = total_time + dur;
		println("  {} ({} sec) -> {} ({} sec)", light_name(prev), light_duration(prev), light_name(state), dur);
	}

	println();
	println("Total simulated time: {} seconds", total_time);

	// Verify the cycle: red -> green -> yellow -> red -> ...
	// Starting at red(30), 9 transitions = 3 full cycles, back to red
	// red->green->yellow->red->green->yellow->red->green->yellow->red
	Light check = Light.red(30);
	check = next_light(check);
	assert light_duration(check) == 25, "after red should be green(25)";
	check = next_light(check);
	assert light_duration(check) == 5, "after green should be yellow(5)";
	check = next_light(check);
	assert light_duration(check) == 30, "after yellow should be red(30)";

	// One full cycle = 30 + 25 + 5 = 60 seconds
	println("One full cycle = 60 seconds (30 + 25 + 5)");

	println();
	println("Traffic light demo passed!");
	return 0;
}
