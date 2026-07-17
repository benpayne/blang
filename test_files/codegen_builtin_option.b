// Built-in Option<T> — no user-defined enum. Exercises Option as a parameter
// type, a return type, some(T)/none construction, and match.

fn first_positive(int a, int b) -> Option<int> {
	if a > 0 {
		return Option.some(a);
	}
	if b > 0 {
		return Option.some(b);
	}
	return Option.none;
}

fn unwrap_or(Option<int> o, int fallback) -> int {
	match o {
		some(v) {
			return v;
		}
		none {
			return fallback;
		}
	}
}

fn main() -> int {
	int a = unwrap_or(first_positive(0 - 5, 7), 0);       // 7 (some)
	int b = unwrap_or(first_positive(0 - 1, 0 - 2), 100); // 100 (none)
	return a + b - 107;
}
