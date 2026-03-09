// E2E test: print/println with various types
// Tests: print builtin generates formatted output to stdout

extern fn strcmp(cstring a, cstring b) -> int;

fn main() -> int {
	// Basic println with no args (empty line)
	println();

	// println with string literal
	println("hello world");

	// print without newline
	print("no newline");
	println();

	// println with int
	int x = 42;
	println("x = {}", x);

	// println with float
	double pi = 3.14159;
	println("pi = {}", pi);

	// println with bool
	bool flag = true;
	println("flag = {}", flag);

	// println with string variable
	string name = "BLang";
	println("name = {}", name);

	// println with multiple args
	int a = 10;
	int b = 20;
	println("{} + {} = {}", a, b, a);

	// println with escaped braces
	println("use {{}} for placeholders");

	println("print codegen test passed!");
	return 0;
}
