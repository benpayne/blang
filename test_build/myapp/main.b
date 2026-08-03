import mathlib;

fn main() -> int {
	int result = add(3, 4);
	println("3 + 4 = {}", result);

	int product = multiply(5, 6);
	println("5 * 6 = {}", product);

	// Generic FUNCTION from the dep: same instantiation the lib itself uses
	// (largest<int>, deduped at link) plus a fresh one (largest<double>).
	println("largest int = {}", largest(9, 4));
	println("largest dbl = {}", largest(2.5, 7.25));

	// Generic STRUCT from the dep: instantiate, call methods.
	Pair<int> p = Pair<int> { first: 10, second: 32 };
	println("sum = {}", p.sum());
	Pair<int> q = p.swap();
	println("swapped = {} {}", q.first, q.second);

	// The lib's own instantiation still works through its public wrapper.
	println("max3 = {}", max3(5, 11, 7));

	// Refcounted instantiations: string > is lexicographic, string + concats.
	println("largest str = {}", largest("apple", "pear"));
	Pair<string> names = Pair<string> { first: "ada ", second: "lovelace" };
	println("concat = {}", names.sum());
	Pair<string> flipped = names.swap();
	println("flipped = {}{}", flipped.first, flipped.second);
	return 0;
}
