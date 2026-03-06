// Demo 4: Result-Based Calculator
// Features: enum payloads, Result type, match/destructuring, error handling

extern fn printf(cstring fmt, ...) -> int;
extern fn puts(cstring s) -> int;

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
			printf("  ERROR: %s\n", msg.to_cstring());
		}
	}
	return out;
}

fn main() -> int {
	printf("=== Result-Based Calculator ===\n\n");

	// Basic operations
	printf("Basic operations:\n");
	Result r1 = add(10, 20);
	match r1 {
		ok(val) { printf("  10 + 20 = %d\n", val); assert val == 30; }
		err(msg) { return 1; }
	}

	Result r2 = multiply(7, 6);
	match r2 {
		ok(val) { printf("  7 * 6 = %d\n", val); assert val == 42; }
		err(msg) { return 1; }
	}

	Result r3 = divide(100, 4);
	match r3 {
		ok(val) { printf("  100 / 4 = %d\n", val); assert val == 25; }
		err(msg) { return 1; }
	}

	printf("\nError handling:\n");

	// Division by zero — should return err
	Result r4 = divide(42, 0);
	match r4 {
		ok(val) {
			printf("  UNEXPECTED: got ok(%d)\n", val);
			return 1;
		}
		err(msg) {
			printf("  42 / 0 = err(\"%s\") -- correctly caught!\n", msg.to_cstring());
		}
	}

	// Modulo by zero
	Result r5 = modulo(10, 0);
	match r5 {
		ok(val) { return 1; }
		err(msg) {
			printf("  10 %% 0 = err(\"%s\") -- correctly caught!\n", msg.to_cstring());
		}
	}

	printf("\nChained computation (((100 - 30) * 2) / 7):\n");

	// Chained: (100 - 30) * 2 / 7 = 70 * 2 / 7 = 140 / 7 = 20
	int step1 = unwrap_or(subtract(100, 30), 0);
	printf("  100 - 30 = %d\n", step1);

	int step2 = unwrap_or(multiply(step1, 2), 0);
	printf("  %d * 2 = %d\n", step1, step2);

	int step3 = unwrap_or(divide(step2, 7), 0);
	printf("  %d / 7 = %d\n", step2, step3);

	assert step3 == 20, "chained computation should equal 20";

	printf("\nResult calculator demo passed!\n");
	return 0;
}
