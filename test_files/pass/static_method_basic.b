struct Counter {
	int value;
}

impl Counter {
	static fn zero() -> Counter {
		return Counter { value: 0 };
	}

	fn get(self) -> int {
		return self.value;
	}
}

fn main() -> int {
	Counter c = Counter.zero();
	return c.get();
}
