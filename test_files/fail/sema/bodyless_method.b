// A bodyless method is an INTERFACE form: it is what a .bmod carries so a
// consumer can resolve an imported type's API. In ordinary source it used to
// codegen an empty function returning zero — a silent wrong answer.
struct Counter {
	int count;
}

impl Counter {
	fn bump(self) -> int;
}

fn main() -> int {
	return 0;
}
