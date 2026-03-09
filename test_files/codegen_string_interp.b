// E2E test: string interpolation
// Tests: "hello {name}" generates formatted string via snprintf

fn main() -> int {
	// Basic string interpolation with integer
	int x = 42;
	string msg = "the answer is {x}";
	println("{}", msg);

	// String interpolation with arithmetic expression variable
	int a = 10;
	int b = 20;
	int sum = a + b;
	string result = "sum is {sum}";
	println("{}", result);

	println("String interpolation codegen test passed!");
	return 0;
}
