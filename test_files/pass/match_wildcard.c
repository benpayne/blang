// Match expression with wildcard pattern

fn test(int x) -> int {
	match x {
		0 { return 0; }
		1 { return 1; }
		_ { return x; }
	}
	return 0;
}

fn main() -> int {
	return 0;
}
