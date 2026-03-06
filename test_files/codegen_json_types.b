extern fn puts(cstring s) -> int;
extern fn printf(cstring fmt, ...) -> int;

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

	if a2.i != 42 { return 1; }
	if a2.b != true { return 2; }
	if a2.c != 65 { return 3; }
	if a2.sh != 100 { return 4; }
	if a2.l != 999999 { return 5; }

	puts("All types JSON test passed!");
	return 0;
}
