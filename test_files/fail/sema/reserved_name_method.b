// Reserved-family check on methods: without it, a method named `__new` would
// mangle onto the factory symbol the defining module emits.
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
