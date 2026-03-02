// E2E test: string interpolation
// Tests: "hello {name}" generates formatted string via snprintf

extern fn printf(string fmt, ...) -> int;
extern fn strcmp(string a, string b) -> int;

fn main() -> int {
	// Basic string interpolation with integer
	int x = 42;
	string msg = "the answer is {x}";
	printf("%s\n", msg);

	// String interpolation with arithmetic expression variable
	int a = 10;
	int b = 20;
	int sum = a + b;
	string result = "sum is {sum}";
	printf("%s\n", result);

	printf("String interpolation codegen test passed!\n");
	return 0;
}
