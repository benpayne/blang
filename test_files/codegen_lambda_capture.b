// Test: lambda capture semantics — value copy, string retain, snapshots
extern fn printf(cstring fmt, ...) -> int;

fn apply(fn(int) -> int f, int x) -> int {
	return f(x);
}

fn get_adder(int base) -> fn(int) -> int {
	// base is captured by value into the lambda context
	fn(int) -> int adder = fn(int x) -> int { return x + base; };
	return adder;
}

fn get_greeter(string name) -> fn() -> string {
	// name is a refcounted string — must be retained in lambda context
	fn() -> string greeter = fn() -> string { return "hello " + name; };
	return greeter;
}

fn main() -> int {
	// Test 1: basic int capture
	int offset = 100;
	fn(int) -> int adder = fn(int x) -> int { return x + offset; };
	int r1 = adder(5);
	if r1 != 105 {
		printf("FAIL test 1: expected 105, got %d\n", r1);
		return 1;
	}

	// Test 2: captured value is a snapshot (not a reference)
	offset = 999;
	int r2 = adder(5);
	if r2 != 105 {
		printf("FAIL test 2: expected 105 (snapshot), got %d\n", r2);
		return 2;
	}

	// Test 3: lambda returned from function (captures survive caller's scope)
	fn(int) -> int add10 = get_adder(10);
	int r3 = add10(7);
	if r3 != 17 {
		printf("FAIL test 3: expected 17, got %d\n", r3);
		return 3;
	}

	// Test 4: string capture survives outer scope
	fn() -> string greet = get_greeter("world");
	string r4 = greet();
	if r4 != "hello world" {
		printf("FAIL test 4: expected 'hello world'\n");
		return 4;
	}

	// Test 5: lambda with no captures
	fn(int) -> int doubler = fn(int x) -> int { return x * 2; };
	int r5 = doubler(21);
	if r5 != 42 {
		printf("FAIL test 5: expected 42, got %d\n", r5);
		return 5;
	}

	// Test 6: multiple captures
	int a = 10;
	int b = 20;
	string prefix = "sum";
	fn() -> int multi = fn() -> int { return a + b; };
	int r6 = multi();
	if r6 != 30 {
		printf("FAIL test 6: expected 30, got %d\n", r6);
		return 6;
	}

	// Test 7: nested capture (lambda captures lambda)
	fn(int) -> int add100 = get_adder(100);
	fn(int) -> int composed = fn(int x) -> int { return add100(x) + 1; };
	int r7 = composed(5);
	if r7 != 106 {
		printf("FAIL test 7: expected 106, got %d\n", r7);
		return 7;
	}

	return 0;
}
