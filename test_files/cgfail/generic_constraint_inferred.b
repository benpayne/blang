// EXPECT-ERROR: does not satisfy constraint 'Container'
protocol Container<T> {
	fn get(self, int index) -> T;
	fn size(self) -> int;
}

struct NotAContainer {
	int x;
}

fn total<C: Container>(C c) -> int {
	return c.size();
}

fn main() -> int {
	NotAContainer n = NotAContainer { x: 1 };
	// No explicit type args: the constraint must be enforced on the INFERRED
	// binding C = NotAContainer.
	return total(n);
}
