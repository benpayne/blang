// Demo 10: Mini Math Library with Contracts
// Features: contracts (requires/ensures), pipeline (|>), assert, multiple functions

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
	println("=== Mini Math Library ===");
	println();

	// abs
	println("abs(-42) = {}", abs(-42));
	println("abs(17) = {}", abs(17));
	assert abs(-42) == 42;
	assert abs(0) == 0;
	assert abs(17) == 17;

	// clamp
	println();
	println("clamp(150, 0, 100) = {}", clamp(150, 0, 100));
	println("clamp(-10, 0, 100) = {}", clamp(-10, 0, 100));
	println("clamp(50, 0, 100) = {}", clamp(50, 0, 100));
	assert clamp(150, 0, 100) == 100;
	assert clamp(-10, 0, 100) == 0;
	assert clamp(50, 0, 100) == 50;

	// factorial
	println();
	println("Factorials:");
	for i in 0..11 {
		println("  {}! = {}", i, factorial(i));
	}
	assert factorial(0) == 1;
	assert factorial(1) == 1;
	assert factorial(5) == 120;
	assert factorial(10) == 3628800;

	// gcd and lcm
	println();
	println("gcd(12, 8) = {}", gcd(12, 8));
	println("lcm(12, 8) = {}", lcm(12, 8));
	assert gcd(12, 8) == 4;
	assert gcd(17, 13) == 1;
	assert gcd(100, 75) == 25;
	assert lcm(12, 8) == 24;
	assert lcm(7, 5) == 35;

	// is_prime
	println();
	print("Primes in 1..30: ");
	for n in 1..31 {
		if is_prime(n) == 1 {
			print("{} ", n);
		}
	}
	println();
	assert is_prime(2) == 1;
	assert is_prime(13) == 1;
	assert is_prime(4) == 0;
	assert is_prime(1) == 0;

	// power
	println();
	println("2^10 = {}", power(2, 10));
	println("3^5 = {}", power(3, 5));
	assert power(2, 10) == 1024;
	assert power(3, 5) == 243;
	assert power(5, 0) == 1;

	// Pipeline composition: 12 |> gcd(8) |> factorial()
	println();
	println("Pipeline: 12 |> gcd(8) |> factorial()");
	int piped = 12 |> gcd(8) |> factorial();
	println("  = gcd(12,8) = 4, then 4! = {}", piped);
	assert piped == 24, "gcd(12,8)=4, 4!=24";

	// Another pipeline: sum_range then clamp
	int total = sum_range(1, 10);
	int clamped = total |> clamp(0, 50);
	println();
	println("sum(1..10) = {}, clamped to [0,50] = {}", total, clamped);
	assert total == 55;
	assert clamped == 50;

	println();
	println("Mini math library demo passed!");
	return 0;
}
