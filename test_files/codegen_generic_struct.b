extern fn puts(string s) -> int;
extern fn printf(string fmt, ...) -> int;

struct Box<T> {
	T value;
}

fn main() -> int {
	Box<int> b1 = Box<int> { value: 42 };
	if b1.value != 42 { return 1; }

	Box<string> b2 = Box<string> { value: "hello" };
	puts(b2.value);

	puts("Generic struct test passed!");
	return 0;
}
