// Demo 6: Generic Pair and Box Types
// Features: generic structs, monomorphization, struct literals, field access

// A generic box holding a single value
struct Box<T> {
	T value;
}

// A generic pair holding two values of the same type
struct Pair<T> {
	T first;
	T second;
}

// Generic identity function
fn identity<T>(T val) -> T {
	return val;
}

// Work with Box<int>
fn unbox_int(Box<int> b) -> int {
	return b.value;
}

// Compute min of a pair of ints
fn min_of_pair(Pair<int> p) -> int {
	if p.first < p.second {
		return p.first;
	}
	return p.second;
}

// Compute max of a pair of ints
fn max_of_pair(Pair<int> p) -> int {
	if p.first > p.second {
		return p.first;
	}
	return p.second;
}

fn main() -> int {
	println("=== Generic Pair and Box Types ===");
	println();

	// Box<int>
	Box<int> b1 = Box<int> { value: 42 };
	println("Box<int> {{{{ value: {} }}}}", b1.value);
	assert b1.value == 42;
	assert unbox_int(b1) == 42;

	// Box<string>
	Box<string> b2 = Box<string> { value: "hello generics" };
	println("Box<string> {{{{ value: \"{}\" }}}}", b2.value);

	// Pair<int>
	Pair<int> p1 = Pair<int> { first: 10, second: 20 };
	println();
	println("Pair<int> {{{{ first: {}, second: {} }}}}", p1.first, p1.second);
	println("  min = {}, max = {}", min_of_pair(p1), max_of_pair(p1));
	assert min_of_pair(p1) == 10;
	assert max_of_pair(p1) == 20;

	// Pair with reversed order
	Pair<int> p2 = Pair<int> { first: 99, second: 3 };
	println("Pair<int> {{{{ first: {}, second: {} }}}}", p2.first, p2.second);
	println("  min = {}, max = {}", min_of_pair(p2), max_of_pair(p2));
	assert min_of_pair(p2) == 3;
	assert max_of_pair(p2) == 99;

	// Pair<string>
	Pair<string> p3 = Pair<string> { first: "alpha", second: "omega" };
	println();
	println("Pair<string> {{{{ first: \"{}\", second: \"{}\" }}}}", p3.first, p3.second);

	// Generic identity function
	int x = identity<int>(777);
	assert x == 777;
	println();
	println("identity<int>(777) = {}", x);

	string s = identity<string>("blang");
	println("identity<string>(\"blang\") = \"{}\"", s);

	// Nested generics: Box containing a value extracted from a Pair
	int extracted = min_of_pair(p1);
	Box<int> boxed = Box<int> { value: extracted };
	println();
	println("Boxed min of pair: {}", boxed.value);
	assert boxed.value == 10;

	println();
	println("Generic pair demo passed!");
	return 0;
}
