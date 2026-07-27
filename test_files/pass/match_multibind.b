enum Pair { two(int, string), one(int), zero }

fn f(Pair p) -> int {
	match p {
		two(n, s) { return n + s.length; }
		one(n) { return n; }
		zero { return 0; }
	}
	return -1;
}

fn main() -> int {
	return f(Pair.two(1, "x"));
}
