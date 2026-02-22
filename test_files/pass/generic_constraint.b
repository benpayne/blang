// Generic function with protocol constraint

protocol Comparable {
	fn compare(self) -> int;
}

fn sort<T: Comparable>(T a, T b) -> T {
	return a;
}

fn main() -> int {
	return 0;
}
