// Demo 3: Collatz Conjecture
// Features: while loops, if/else, contracts (requires), assert, printf

extern fn printf(cstring fmt, ...) -> int;

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
	printf("=== Collatz Conjecture ===\n\n");

	// Show the sequence for a few starting values
	printf("Collatz sequence lengths:\n");
	for i in 1..21 {
		int steps = collatz_steps(i);
		printf("  collatz(%d) = %d steps\n", i, steps);
	}

	printf("\n");

	// Known values
	assert collatz_steps(1) == 0;
	assert collatz_steps(2) == 1;
	assert collatz_steps(3) == 7;
	assert collatz_steps(6) == 8;
	assert collatz_steps(7) == 16;
	assert collatz_steps(27) == 111;

	printf("Known values verified.\n\n");

	// Find the number with the longest sequence in 1..100
	int winner = longest_collatz(1, 100);
	int winner_steps = collatz_steps(winner);
	printf("Longest Collatz sequence in 1..100:\n");
	printf("  n = %d with %d steps\n", winner, winner_steps);

	// 97 has the longest sequence in 1..100 (118 steps)
	assert winner == 97;
	assert winner_steps == 118;

	// Trace the full sequence for n=27 (a famously long one)
	printf("\nFull sequence for n=27 (%d steps):\n  ", collatz_steps(27));
	int current = 27;
	int printed = 0;
	while current != 1 {
		printf("%d -> ", current);
		printed = printed + 1;
		if printed % 8 == 0 {
			printf("\n  ");
		}
		if current % 2 == 0 {
			current = current / 2;
		} else {
			current = current * 3 + 1;
		}
	}
	printf("1\n");

	printf("\nCollatz conjecture demo passed!\n");
	return 0;
}
