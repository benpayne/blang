// Generic protocol end to end: protocol Container<T> with a wildcard return
// (T), two conforming structs, and a constrained generic fn total<C: Container>
// whose method calls on the C-typed param dispatch to each concrete struct
// (inference binds C per call; the constraint is checked on the inferred arg).
// generic protocol + constrained generic fn dispatching over two structs
protocol Container<T> {
	fn get(self, int index) -> T;
	fn size(self) -> int;
}

struct IntList {
	Array<int> items;
}

impl Container for IntList {
	fn get(self, int index) -> int {
		return self.items[index];
	}
	fn size(self) -> int {
		return self.items.length;
	}
}

struct Repeat {
	int value;
	int count;
}

impl Container for Repeat {
	fn get(self, int index) -> int {
		return self.value;
	}
	fn size(self) -> int {
		return self.count;
	}
}

fn total<C: Container>(C c) -> int {
	int sum = 0;
	for i in 0..c.size() {
		sum = sum + c.get(i);
	}
	return sum;
}

fn main() -> int {
	IntList xs = IntList { items: [1, 2, 3, 4] };
	Repeat r = Repeat { value: 7, count: 3 };
	println("{}", total(xs));
	println("{}", total(r));
	return 0;
}
