struct Foo {
	int x;
	int y;
}

impl Foo {
	init(int a, int b) {
		self.x = a;
		self.y = b;
	}

	fn sum(self) -> int {
		return self.x + self.y;
	}
}

fn main() -> int {
	Foo f = Foo(3, 4);
	return f.sum();
}
