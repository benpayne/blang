// End-to-end codegen test for Phase 2 features
// Tests: assert, ownership qualifiers, spawn, contracts

fn validate(int x) -> int requires x > 0 {
	return x;
}

fn main() -> int {
	// Ownership qualifiers (all generate as value types)
	own int a = 1;
	shared int b = 2;
	sync int c = 3;
	int d = 4;

	// Spawn block (inline execution)
	spawn {
		int x = 42;
	}

	// Assert statements
	assert a == 1;
	assert b == 2, "b should be 2";
	assert c == 3;

	// Function with requires contract
	int result = validate(5);

	println("Phase 2 codegen test passed!");
	return 0;
}
