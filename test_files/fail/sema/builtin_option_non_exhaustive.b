// Exhaustiveness checking applies to the built-in Option<T> too: a match that
// omits the none variant without a wildcard must be rejected.

fn lookup(int id) -> Option<int> {
	return Option.some(id);
}

fn main() -> int {
	match lookup(5) {
		some(v) {
			return v;
		}
	}
	return 0;
}
