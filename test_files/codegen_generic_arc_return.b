// Generic ARC: refcounted values returned out of generic functions.
// pick_first returns a borrowed array element as T=string (return-retain via
// the substitution-aware predicates); identity passes an owned string through.
// Both are called with INFERRED type arguments — previously an inference-less
// generic call was silently dropped (exit 0, uninitialized result); now it
// either infers or fails the compile loudly.

fn pick_first<T>(Array<T> items) -> T {
	T first = items[0];
	return first;
}

fn identity<T>(T value) -> T {
	return value;
}

fn main() -> int {
	Array<string> words = ["alpha", "beta", "gamma"];

	string w = pick_first(words);        // inferred: pick_first<string>
	println("{}", w);

	string x = identity<string>(w);      // explicit args still work
	println("{}", x);

	int n = identity(42);                // inferred from a literal
	println("{}", n);

	// The array still owns intact elements after the borrows above.
	for word in words {
		println("{}", word);
	}
	return 0;
}
