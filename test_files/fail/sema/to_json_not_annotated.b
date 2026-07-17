// to_json requires a @json-annotated struct. Calling it on a plain struct is
// a compile error.

struct Plain {
	int x;
}

fn main() -> int {
	Plain p = Plain { x: 1 };
	string s = to_json(p);
	return 0;
}
