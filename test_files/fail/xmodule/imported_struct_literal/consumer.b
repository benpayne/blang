import lib;

fn main() -> int {
	// A struct literal names every field, so it cannot be written outside the
	// defining module — construct with Counter(5) instead.
	Counter c = Counter { count: 5 };
	return c.bump();
}
