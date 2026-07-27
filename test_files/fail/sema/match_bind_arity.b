// EXPECT-ERROR: binds 3 values but variant has 2
enum Pair { two(int, int) }
fn main() -> int {
	match Pair.two(1, 2) {
		two(a, b, c) { return a; }
	}
	return 0;
}
