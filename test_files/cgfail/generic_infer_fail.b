// A generic call whose type arguments can be neither inferred from the
// arguments nor were given explicitly is a LOUD compile error. Previously the
// call was silently dropped (stderr note, exit 0) and the consumer read
// uninitialized memory.
// EXPECT-ERROR: cannot infer type arguments for generic function 'make_default'

fn make_default<T>() -> T {
	T value = 0;
	return value;
}

fn main() -> int {
	int x = make_default();
	return x;
}
