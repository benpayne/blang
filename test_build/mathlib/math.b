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
	// Construction via `pub init` — the one cross-module construction spelling
	// for a generic struct now that imported struct literals are private (U5b).
	// A consumer writes `Pair<int>(10, 32)`, which monomorphizes Pair for the
	// written type args and calls this init.
	pub init(T first, T second) {
		self.first = first;
		self.second = second;
	}

	// Accessor methods — the member-variable read surface across a .bmod
	// boundary (U5b: `q.first` is now module-private; a consumer reads via
	// `q.first()`). Generic structs ship all method bodies in the .bmod, so a
	// consumer monomorphizes these on demand.
	fn first(self) -> T {
		return self.first;
	}

	fn second(self) -> T {
		return self.second;
	}

	fn sum(self) -> T {
		return self.first + self.second;
	}

	fn swap(self) -> Pair<T> {
		// Same-module construction keeps the struct-literal form (field/literal
		// privacy is .bmod-path-only; Pair is not from-interface here).
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
