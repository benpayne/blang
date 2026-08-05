// Reserved-family check on methods. The rule is uniform rather than
// collision-driven: a method's own mangling (`Counter___dtor`) would not clash
// with a generated symbol, but allowing `__` names in source would make the
// reservation a convention the compiler does not actually hold, and reviewers
// could not tell a generated symbol from a hand-written one.
struct Counter {
	int count;
}

impl Counter {
	fn __dtor(self) -> int {
		return self.count;
	}
}

fn main() -> int {
	return 0;
}
