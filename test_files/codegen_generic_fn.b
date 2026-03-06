extern fn puts(string s) -> int;

fn identity<T>(T val) -> T {
	return val;
}

fn main() -> int {
	int x = identity<int>(42);
	if x != 42 { return 1; }

	string s = identity<string>("hello");
	puts(s);

	puts("Generic function test passed!");
	return 0;
}
