// Builtin to_json(value): compile-time dispatch to StructName_to_json for a
// @json-annotated struct. Covers primitive fields and a nested @json struct.

extern fn puts(cstring s) -> int;

@json
struct Address {
	string city;
	int zip;
}

@json
struct Person {
	string name;
	int age;
	Address address;
}

fn main() -> int {
	Person p = Person {
		name: "ada",
		age: 36,
		address: Address { city: "london", zip: 12345 }
	};

	string s = to_json(p);
	puts(s);

	if s.contains("ada") != true { return 1; }
	if s.contains("36") != true { return 2; }
	if s.contains("london") != true { return 3; }
	if s.contains("12345") != true { return 4; }

	// Standalone @json struct too
	Address a = Address { city: "paris", zip: 75001 };
	string sa = to_json(a);
	if sa.contains("paris") != true { return 5; }

	puts("to_json builtin test passed!");
	return 0;
}
