pub fn add(int a, int b) -> int {
	return a + b;
}

pub fn multiply(int a, int b) -> int {
	return a * b;
}

fn internal_helper(int x) -> int {
	return x + 1;
}

// Cross-module generics: shipped with bodies in the .bmod so consumers can
// monomorphize them.
pub fn largest<T>(T a, T b) -> T {
	if a > b {
		return a;
	}
	return b;
}

// A user-defined protocol conformed to by a GENERIC struct. This is what gives
// the "generic bodies + conformances" golden check its second half: before this
// the golden contained zero conformance records, so the generic conformance path
// M-1 opened was emitted but never exercised.
pub protocol Summable {
	fn total(self) -> int;
}

pub struct Pair<T> {
	T first;
	T second;
}

impl Pair {
	fn sum(self) -> T {
		return self.first + self.second;
	}

	fn swap(self) -> Pair<T> {
		return Pair<T> { first: self.second, second: self.first };
	}
}

impl Summable for Pair {
	fn total(self) -> int {
		return 0;
	}
}

// The library itself instantiates largest<int> — the consumer does too, so
// the linkonce_odr instances must dedup at link time.
pub fn max3(int a, int b, int c) -> int {
	return largest(largest(a, b), c);
}
