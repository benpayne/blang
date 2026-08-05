// Same rule for constructors: `init(...);` without a body is only valid in a
// .bmod interface file, where it declares the library-emitted factory.
struct Counter {
	int count;
}

impl Counter {
	init(int start);
}

fn main() -> int {
	return 0;
}
