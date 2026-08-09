import lib;

fn main() -> int {
	Counter c = Counter(5);
	// `count` is private to lib.b — fields never cross a module boundary (D9).
	return c.count;
}
