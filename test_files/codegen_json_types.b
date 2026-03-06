extern fn puts(string s) -> int;
extern fn free(string s);
extern fn printf(string fmt, ...) -> int;

@json
struct AllTypes {
	int i;
	float f;
	double d;
	string s;
	bool b;
	char c;
	short sh;
	long l;
}

fn main() -> int {
	AllTypes a = AllTypes { i: 42, f: 3.14, d: 2.718, s: "hello", b: true, c: 65, sh: 100, l: 999999 };
	string json = AllTypes_to_json(a);
	puts(json);

	AllTypes a2 = AllTypes_from_json(json);
	free(json);

	if a2.i != 42 { return 1; }

	puts("All types JSON test passed!");
	return 0;
}
