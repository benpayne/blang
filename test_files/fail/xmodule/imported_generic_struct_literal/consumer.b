import lib;

fn main() -> int {
	// A struct literal for an imported generic type is not permitted — the one
	// construction spelling is Pair<int>(10, 32) via `pub init`.
	Pair<int> p = Pair<int> { first: 10, second: 32 };
	return 0;
}
