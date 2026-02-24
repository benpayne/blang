// Match expression with destructuring patterns

fn run_test(int status) -> int {
	match status {
		ok(value) {
			return value;
		}
		err(e) {
			return 0;
		}
	}
	return 0;
}

fn main() -> int {
	return 0;
}
