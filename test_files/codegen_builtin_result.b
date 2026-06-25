// Built-in Result<T, E> — no user-defined enum. Exercises ok(T)/err(E)
// construction with a string error payload and match with bindings.

fn divide(int a, int b) -> Result<int, string> {
	if b == 0 {
		return Result.err("divide by zero");
	}
	return Result.ok(a / b);
}

fn main() -> int {
	int sum = 0;

	match divide(20, 4) {
		ok(v) {
			sum = sum + v;          // +5
		}
		err(msg) {
			return 10;
		}
	}

	match divide(1, 0) {
		ok(v) {
			return 20;
		}
		err(msg) {
			if msg == "divide by zero" {
				sum = sum + 37;     // +37 -> 42
			}
		}
	}

	return sum - 42;
}
