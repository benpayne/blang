// Demo 3: Collatz Conjecture
// Features: while loops, if/else, contracts (requires), assert, print/println

// Compute the Collatz sequence length for a starting number.
// The conjecture says every positive integer eventually reaches 1.
fn collatz_steps(int n) -> int requires n > 0 {
	int steps = 0;
	int current = n;
	while current != 1 {
		if current % 2 == 0 {
			current = current / 2;
		} else {
			current = current * 3 + 1;
		}
		steps = steps + 1;
	}
	return steps;
}

// Find the number with the longest Collatz sequence in a range
fn longest_collatz(int from, int to) -> int requires from > 0 {
	int best_n = from;
	int best_steps = 0;
	int n = from;
	while n <= to {
		int steps = collatz_steps(n);
		if steps > best_steps {
			best_steps = steps;
			best_n = n;
		}
		n = n + 1;
	}
	return best_n;
}

fn main() -> int {
	println("=== Collatz Conjecture ===");
	println();

	// Show the sequence for a few starting values
	println("Collatz sequence lengths:");
	for i in 1..21 {
		int steps = collatz_steps(i);
		println("  collatz({}) = {} steps", i, steps);
	}

	println();

	// Known values
	assert collatz_steps(1) == 0;
	assert collatz_steps(2) == 1;
	assert collatz_steps(3) == 7;
	assert collatz_steps(6) == 8;
	assert collatz_steps(7) == 16;
	assert collatz_steps(27) == 111;

	println("Known values verified.");
	println();

	// Find the number with the longest sequence in 1..100
	int winner = longest_collatz(1, 100);
	int winner_steps = collatz_steps(winner);
	println("Longest Collatz sequence in 1..100:");
	println("  n = {} with {} steps", winner, winner_steps);

	// 97 has the longest sequence in 1..100 (118 steps)
	assert winner == 97;
	assert winner_steps == 118;

	// Trace the full sequence for n=27 (a famously long one)
	println();
	print("Full sequence for n=27 ({} steps):\n  ", collatz_steps(27));
	int current = 27;
	int printed = 0;
	while current != 1 {
		print("{} -> ", current);
		printed = printed + 1;
		if printed % 8 == 0 {
			print("\n  ");
		}
		if current % 2 == 0 {
			current = current / 2;
		} else {
			current = current * 3 + 1;
		}
	}
	println("1");

	println();
	println("Collatz conjecture demo passed!");
	return 0;
}
