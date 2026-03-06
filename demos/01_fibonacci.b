// Demo 1: Fibonacci Sequence
// Features: recursion, for-in range, if/else, string interpolation, printf

extern fn printf(cstring fmt, ...) -> int;

// Recursive fibonacci
fn fib(int n) -> int {
	if n <= 1 {
		return n;
	}
	return fib(n - 1) + fib(n - 2);
}

// Iterative fibonacci — much faster for larger N
fn fib_iter(int n) -> int {
	if n <= 1 {
		return n;
	}
	int prev = 0;
	int curr = 1;
	for i in 2..n {
		int next = prev + curr;
		prev = curr;
		curr = next;
	}
	// one more iteration for the final step (range is exclusive)
	int next = prev + curr;
	return next;
}

fn main() -> int {
	printf("=== Fibonacci Sequence ===\n\n");

	// Print first 20 fibonacci numbers using recursion
	printf("Recursive fibonacci (0..20):\n");
	for i in 0..20 {
		int val = fib(i);
		printf("  fib(%d) = %d\n", i, val);
	}

	printf("\n");

	// Verify iterative matches recursive
	printf("Verifying iterative matches recursive...\n");
	for i in 0..20 {
		int r = fib(i);
		int it = fib_iter(i);
		assert r == it, "iterative should match recursive";
	}
	printf("  All 20 values match!\n");

	// Some specific checks
	assert fib(0) == 0;
	assert fib(1) == 1;
	assert fib(10) == 55;
	assert fib(15) == 610;

	printf("\nFibonacci demo passed!\n");
	return 0;
}
