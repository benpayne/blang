// KI-22 (modules-v2-exports U5b): a for-in loop whose iterable is a method
// call on a generic struct returning Array<K>. Pre-fix, the loop-var element
// type was not resolved (the method-call case was missing from the for-in
// element-type inference), so the receiver's type arguments were not
// substituted through the method's Array<K> return: the loop var defaulted to
// int and a `string` element was read as an integer (garbage), then
// dereferenced (segfault). Post-fix, `for k in m.keys()` binds k as string.
//
// This is the idiomatic opaque-collection iteration spelling the corpus
// migration relies on (no intermediate-typed-var workaround needed).

import collections;

fn main() -> int {
	Map<string, int> m = Map<string, int> { keys: [], values: [], buckets: [] };
	m.set("alpha", 1);
	m.set("beta", 2);
	m.set("gamma", 3);

	// Direct for-in over the method call — the KI-22 shape.
	for k in m.keys() {
		println("key={}", k);
	}

	// Values (Array<V>, V=int) direct method-call iteration too.
	int total = 0;
	for v in m.values() {
		total = total + v;
	}
	println("total={}", total);

	// Set<K>.items() — the same shape on Set.
	Set<string> s = Set<string> { items: [], buckets: [] };
	s.add("x");
	s.add("y");
	for it in s.items() {
		println("item={}", it);
	}

	println("PASS");
	return 0;
}
