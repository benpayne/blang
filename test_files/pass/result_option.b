// Test Result and Option as enum types

enum Result<T, E> {
	ok(T),
	err(E)
}

enum Option<T> {
	some(T),
	none
}

fn divide(int a, int b) -> Result<int, string> {
	return 42;
}

fn find_item(int id) -> Option<int> {
	return 0;
}

fn handle_result() -> int {
	var result = divide(10, 2);
	match result {
		ok(value) {
			return value;
		}
		err(msg) {
			return 0;
		}
	}
	return 0;
}

fn handle_option() -> int {
	var item = find_item(1);
	match item {
		some(val) {
			return val;
		}
		none {
			return 0;
		}
	}
	return 0;
}

fn main() -> int {
	return 0;
}
