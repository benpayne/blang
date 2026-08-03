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

// The library itself instantiates largest<int> — the consumer does too, so
// the linkonce_odr instances must dedup at link time.
pub fn max3(int a, int b, int c) -> int {
	return largest(largest(a, b), c);
}
