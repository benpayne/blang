// Demo 10: Mini Math Library with Contracts
// Features: contracts (requires/ensures), pipeline (|>), assert, multiple functions

extern fn printf(cstring fmt, ...) -> int;

fn abs(int x) -> int ensures result >= 0 {
	if x < 0 {
		return 0 - x;
	}
	return x;
}

fn clamp(int x, int lo, int hi) -> int requires lo <= hi {
	if x < lo {
		return lo;
	}
	if x > hi {
		return hi;
	}
	return x;
}

fn factorial(int n) -> int requires n >= 0 {
	if n <= 1 {
		return 1;
	}
	return n * factorial(n - 1);
}

fn gcd(int a, int b) -> int requires a > 0 requires b > 0 {
	int x = a;
	int y = b;
	while y != 0 {
		int temp = y;
		y = x % y;
		x = temp;
	}
	return x;
}

fn lcm(int a, int b) -> int requires a > 0 requires b > 0 {
	return (a / gcd(a, b)) * b;
}

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
	int i = 3;
	while i * i <= n {
		if n % i == 0 {
			return 0;
		}
		i = i + 2;
	}
	return 1;
}

fn power(int base, int exp) -> int requires exp >= 0 {
	int result = 1;
	for i in 0..exp {
		result = result * base;
	}
	return result;
}

fn sum_range(int from, int to) -> int {
	int total = 0;
	int i = from;
	while i <= to {
		total = total + i;
		i = i + 1;
	}
	return total;
}

// Pipeline-friendly wrappers (take first arg as subject)
fn add(int x, int y) -> int {
	return x + y;
}

fn multiply(int x, int y) -> int {
	return x * y;
}

fn main() -> int {
	printf("=== Mini Math Library ===\n\n");

	// abs
	printf("abs(-42) = %d\n", abs(-42));
	printf("abs(17) = %d\n", abs(17));
	assert abs(-42) == 42;
	assert abs(0) == 0;
	assert abs(17) == 17;

	// clamp
	printf("\nclamp(150, 0, 100) = %d\n", clamp(150, 0, 100));
	printf("clamp(-10, 0, 100) = %d\n", clamp(-10, 0, 100));
	printf("clamp(50, 0, 100) = %d\n", clamp(50, 0, 100));
	assert clamp(150, 0, 100) == 100;
	assert clamp(-10, 0, 100) == 0;
	assert clamp(50, 0, 100) == 50;

	// factorial
	printf("\nFactorials:\n");
	for i in 0..11 {
		printf("  %d! = %d\n", i, factorial(i));
	}
	assert factorial(0) == 1;
	assert factorial(1) == 1;
	assert factorial(5) == 120;
	assert factorial(10) == 3628800;

	// gcd and lcm
	printf("\ngcd(12, 8) = %d\n", gcd(12, 8));
	printf("lcm(12, 8) = %d\n", lcm(12, 8));
	assert gcd(12, 8) == 4;
	assert gcd(17, 13) == 1;
	assert gcd(100, 75) == 25;
	assert lcm(12, 8) == 24;
	assert lcm(7, 5) == 35;

	// is_prime
	printf("\nPrimes in 1..30: ");
	for n in 1..31 {
		if is_prime(n) == 1 {
			printf("%d ", n);
		}
	}
	printf("\n");
	assert is_prime(2) == 1;
	assert is_prime(13) == 1;
	assert is_prime(4) == 0;
	assert is_prime(1) == 0;

	// power
	printf("\n2^10 = %d\n", power(2, 10));
	printf("3^5 = %d\n", power(3, 5));
	assert power(2, 10) == 1024;
	assert power(3, 5) == 243;
	assert power(5, 0) == 1;

	// Pipeline composition: 12 |> gcd(8) |> factorial()
	printf("\nPipeline: 12 |> gcd(8) |> factorial()\n");
	int piped = 12 |> gcd(8) |> factorial();
	printf("  = gcd(12,8) = 4, then 4! = %d\n", piped);
	assert piped == 24, "gcd(12,8)=4, 4!=24";

	// Another pipeline: sum_range then clamp
	int total = sum_range(1, 10);
	int clamped = total |> clamp(0, 50);
	printf("\nsum(1..10) = %d, clamped to [0,50] = %d\n", total, clamped);
	assert total == 55;
	assert clamped == 50;

	printf("\nMini math library demo passed!\n");
	return 0;
}
