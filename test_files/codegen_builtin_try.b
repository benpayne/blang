// Built-in Result<T, E> with the ? try operator — no user-defined enum.
// checked(x)? unwraps ok or returns the err early.

fn checked(int x) -> Result<int, int> {
	if x < 0 {
		return Result.err(0 - 1);
	}
	return Result.ok(x * 2);
}

fn process(int x) -> Result<int, int> {
	int doubled = checked(x)?;
	return Result.ok(doubled + 1);
}

fn main() -> int {
	int r = 0;

	match process(5) {
		ok(v) {
			r = v;                  // checked(5)=ok(10) -> ok(11) -> r=11
		}
		err(e) {
			return 50;
		}
	}

	match process(0 - 3) {
		ok(v) {
			return 60;
		}
		err(e) {
			r = r + e;              // err(-1) propagated -> 11 + (-1) = 10
		}
	}

	return r - 10;
}
