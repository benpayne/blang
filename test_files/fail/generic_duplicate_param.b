// Using the same type parameter name twice in a generic function is an error

fn foo<T, T>(T x) -> T {
	return x;
}

fn main() -> int {
	return 0;
}
