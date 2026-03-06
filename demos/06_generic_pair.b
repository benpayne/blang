// Demo 6: Generic Pair and Box Types
// Features: generic structs, monomorphization, struct literals, field access

extern fn printf(cstring fmt, ...) -> int;
extern fn puts(cstring s) -> int;

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
	printf("=== Generic Pair and Box Types ===\n\n");

	// Box<int>
	Box<int> b1 = Box<int> { value: 42 };
	printf("Box<int> { value: %d }\n", b1.value);
	assert b1.value == 42;
	assert unbox_int(b1) == 42;

	// Box<string>
	Box<string> b2 = Box<string> { value: "hello generics" };
	printf("Box<string> { value: \"%s\" }\n", b2.value.to_cstring());

	// Pair<int>
	Pair<int> p1 = Pair<int> { first: 10, second: 20 };
	printf("\nPair<int> { first: %d, second: %d }\n", p1.first, p1.second);
	printf("  min = %d, max = %d\n", min_of_pair(p1), max_of_pair(p1));
	assert min_of_pair(p1) == 10;
	assert max_of_pair(p1) == 20;

	// Pair with reversed order
	Pair<int> p2 = Pair<int> { first: 99, second: 3 };
	printf("Pair<int> { first: %d, second: %d }\n", p2.first, p2.second);
	printf("  min = %d, max = %d\n", min_of_pair(p2), max_of_pair(p2));
	assert min_of_pair(p2) == 3;
	assert max_of_pair(p2) == 99;

	// Pair<string>
	Pair<string> p3 = Pair<string> { first: "alpha", second: "omega" };
	printf("\nPair<string> { first: \"%s\", second: \"%s\" }\n", p3.first.to_cstring(), p3.second.to_cstring());

	// Generic identity function
	int x = identity<int>(777);
	assert x == 777;
	printf("\nidentity<int>(777) = %d\n", x);

	string s = identity<string>("blang");
	printf("identity<string>(\"blang\") = \"%s\"\n", s.to_cstring());

	// Nested generics: Box containing a value extracted from a Pair
	int extracted = min_of_pair(p1);
	Box<int> boxed = Box<int> { value: extracted };
	printf("\nBoxed min of pair: %d\n", boxed.value);
	assert boxed.value == 10;

	printf("\nGeneric pair demo passed!\n");
	return 0;
}
