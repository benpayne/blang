struct Pair<A, B> {
	A first;
	B second;
}

impl Pair {
	fn get_first(self) -> A {
		return self.first;
	}

	fn get_second(self) -> B {
		return self.second;
	}

	fn set_first(self, A val) {
		self.first = val;
	}
}

fn main() -> int {
	Pair<int, string> p = Pair<int, string> { first: 42, second: "hello" };

	// Test read methods on generic struct
	if p.get_first() != 42 { return 1; }

	// Test mutation via generic method
	p.set_first(100);
	if p.get_first() != 100 { return 2; }
	if p.first != 100 { return 3; }

	println("Generic struct methods test passed!");
	return 0;
}
