extern fn puts(cstring s) -> int;

@json
struct Inner {
	int x;
	string label;
}

@json
struct Outer {
	string name;
	Inner nested;
	int count;
}

fn main() -> int {
	Inner inner = Inner { x: 42, label: "test" };
	Outer outer = Outer { name: "wrapper", nested: inner, count: 7 };

	string json = Outer_to_json(outer);
	puts(json);

	Outer o2 = Outer_from_json(json);

	if o2.count != 7 { return 1; }
	if o2.nested.x != 42 { return 2; }

	puts("Nested JSON test passed!");
	return 0;
}
