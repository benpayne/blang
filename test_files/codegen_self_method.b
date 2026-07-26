// Test: self.method() calls within the same impl block

extern fn printf(cstring fmt, ...) -> int;

struct Counter {
	int value;
}

impl Counter {
	fn get(self) -> int {
		return self.value;
	}

	fn increment(self) -> int {
		int cur = self.get();
		return cur + 1;
	}

	fn add(self, int n) -> int {
		int cur = self.get();
		return cur + n;
	}

	fn double_increment(self) -> int {
		int first = self.increment();
		int second = self.add(2);
		return first + second;
	}
}

fn main() -> int {
	Counter c = Counter { value: 10 };

	// Test self.get() - basic self method call
	int v = c.get();
	if v != 10 { printf("FAIL: get() returned %d, expected 10\n", v); return 1; }

	// Test self.get() called from increment() - self.method() in same impl
	int inc = c.increment();
	if inc != 11 { printf("FAIL: increment() returned %d, expected 11\n", inc); return 1; }

	// Test self.get() called from add() - self.method() with args
	int added = c.add(5);
	if added != 15 { printf("FAIL: add(5) returned %d, expected 15\n", added); return 1; }

	// Test chained self.method() calls within one method
	int dbl = c.double_increment();
	if dbl != 23 { printf("FAIL: double_increment() returned %d, expected 23\n", dbl); return 1; }

	printf("All self.method() tests passed!\n");
	return 0;
}
