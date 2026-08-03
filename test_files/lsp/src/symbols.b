// LSP fixture: documentSymbol tree — struct (fields + methods), enum
// (variants), protocol (required methods), functions, and a test block.
struct Counter {
	int count;
	int step;
}

impl Counter {
	fn bump(self) -> int {
		return self.count + self.step;
	}
}

enum Shape {
	circle(int),
	square(int),
	empty
}

protocol Sized {
	fn size(self) -> int;
}

fn area_hint(int r) -> int {
	return r * r;
}

fn main() -> int {
	return 0;
}

test "counter bumps" {
	Counter c = Counter { count: 1, step: 2 };
	assert c.bump() == 3;
}
