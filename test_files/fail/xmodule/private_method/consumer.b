import lib;

fn main() -> int {
	Counter c = Counter(1);
	c.bump();
	// `reset` is private to lib.b — not in the interface, not reachable here.
	c.reset();
	return 0;
}
