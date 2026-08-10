import mathlib;

fn main() -> int {
	// modules-v2-graph U6a: a dependency's free functions are reached QUALIFIED
	// (module.name) after `import module;` — the flat merge is retired.
	int result = mathlib.add(3, 4);
	println("3 + 4 = {}", result);

	int product = mathlib.multiply(5, 6);
	println("5 * 6 = {}", product);

	// Generic FUNCTION from the dep: same instantiation the lib itself uses
	// (largest<int>, deduped at link) plus a fresh one (largest<double>).
	println("largest int = {}", mathlib.largest(9, 4));
	println("largest dbl = {}", mathlib.largest(2.5, 7.25));

	// Generic STRUCT from the dep: construct via `pub init`, read via accessors.
	// U5b: an imported struct's fields are private — `Pair<int>(10, 32)` is the
	// one construction spelling and `q.first()`/`q.second()` the read surface.
	Pair<int> p = Pair<int>(10, 32);
	println("sum = {}", p.sum());
	Pair<int> q = p.swap();
	println("swapped = {} {}", q.first(), q.second());

	// The lib's own instantiation still works through its public wrapper.
	println("max3 = {}", mathlib.max3(5, 11, 7));

	// Refcounted instantiations: string > is lexicographic, string + concats.
	println("largest str = {}", mathlib.largest("apple", "pear"));
	Pair<string> names = Pair<string>("ada ", "lovelace");
	println("concat = {}", names.sum());
	Pair<string> flipped = names.swap();
	println("flipped = {}{}", flipped.first(), flipped.second());
	return 0;
}
