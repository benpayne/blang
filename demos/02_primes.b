// Demo 2: Prime Number Finder
// Features: nested loops, while, break, modulo, functions, for-in range

extern fn printf(cstring fmt, ...) -> int;

fn is_prime(int n) -> int {
	if n < 2 {
		return 0;
	}
	if n == 2 {
		return 1;
	}
	if n % 2 == 0 {
		return 0;
	}
	// Trial division up to sqrt(n)
	// We check i*i <= n instead of computing sqrt
	int i = 3;
	while i * i <= n {
		if n % i == 0 {
			return 0;
		}
		i = i + 2;
	}
	return 1;
}

fn count_primes_up_to(int limit) -> int {
	int count = 0;
	for n in 2..limit {
		if is_prime(n) == 1 {
			count = count + 1;
		}
	}
	// Check the limit itself
	if is_prime(limit) == 1 {
		count = count + 1;
	}
	return count;
}

fn main() -> int {
	printf("=== Prime Number Finder ===\n\n");

	// Print primes up to 100
	printf("Primes up to 100:\n  ");
	int printed = 0;
	for n in 2..101 {
		if is_prime(n) == 1 {
			printf("%d ", n);
			printed = printed + 1;
			// Newline every 10 primes for readability
			if printed % 10 == 0 {
				printf("\n  ");
			}
		}
	}
	printf("\n\n");

	// There are 25 primes below 100
	int count = count_primes_up_to(100);
	printf("Count of primes up to 100: %d\n", count);
	assert count == 25, "there are 25 primes up to 100";

	// Verify some known primes
	assert is_prime(2) == 1;
	assert is_prime(3) == 1;
	assert is_prime(97) == 1;
	assert is_prime(4) == 0;
	assert is_prime(100) == 0;
	assert is_prime(1) == 0;

	// Count primes up to 1000
	int big_count = count_primes_up_to(1000);
	printf("Count of primes up to 1000: %d\n", big_count);
	assert big_count == 168, "there are 168 primes up to 1000";

	printf("\nPrime number demo passed!\n");
	return 0;
}
