// Demo 4: Result-Based Calculator
// Features: enum payloads, Result type, match/destructuring, error handling

enum Result {
	ok(int),
	err(string)
}

fn add(int a, int b) -> Result {
	return Result.ok(a + b);
}

fn subtract(int a, int b) -> Result {
	return Result.ok(a - b);
}

fn multiply(int a, int b) -> Result {
	return Result.ok(a * b);
}

fn divide(int a, int b) -> Result {
	if b == 0 {
		return Result.err("division by zero");
	}
	return Result.ok(a / b);
}

fn modulo(int a, int b) -> Result {
	if b == 0 {
		return Result.err("modulo by zero");
	}
	return Result.ok(a % b);
}

// Unwrap a result, printing error and returning fallback on failure
fn unwrap_or(Result r, int fallback) -> int {
	int out = fallback;
	match r {
		ok(val) {
			out = val;
		}
		err(msg) {
			println("  ERROR: {}", msg);
		}
	}
	return out;
}

fn main() -> int {
	println("=== Result-Based Calculator ===");
	println();

	// Basic operations
	println("Basic operations:");
	Result r1 = add(10, 20);
	match r1 {
		ok(val) { println("  10 + 20 = {}", val); assert val == 30; }
		err(msg) { return 1; }
	}

	Result r2 = multiply(7, 6);
	match r2 {
		ok(val) { println("  7 * 6 = {}", val); assert val == 42; }
		err(msg) { return 1; }
	}

	Result r3 = divide(100, 4);
	match r3 {
		ok(val) { println("  100 / 4 = {}", val); assert val == 25; }
		err(msg) { return 1; }
	}

	println();
	println("Error handling:");

	// Division by zero — should return err
	Result r4 = divide(42, 0);
	match r4 {
		ok(val) {
			println("  UNEXPECTED: got ok({})", val);
			return 1;
		}
		err(msg) {
			println("  42 / 0 = err(\"{}\") -- correctly caught!", msg);
		}
	}

	// Modulo by zero
	Result r5 = modulo(10, 0);
	match r5 {
		ok(val) { return 1; }
		err(msg) {
			println("  10 % 0 = err(\"{}\") -- correctly caught!", msg);
		}
	}

	println();
	println("Chained computation (((100 - 30) * 2) / 7):");

	// Chained: (100 - 30) * 2 / 7 = 70 * 2 / 7 = 140 / 7 = 20
	int step1 = unwrap_or(subtract(100, 30), 0);
	println("  100 - 30 = {}", step1);

	int step2 = unwrap_or(multiply(step1, 2), 0);
	println("  {} * 2 = {}", step1, step2);

	int step3 = unwrap_or(divide(step2, 7), 0);
	println("  {} / 7 = {}", step2, step3);

	assert step3 == 20, "chained computation should equal 20";

	println();
	println("Result calculator demo passed!");
	return 0;
}
