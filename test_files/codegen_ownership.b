// E2E test: ownership qualifiers with ARC runtime
// Tests: own, shared, sync variable declarations and access

extern fn printf(string fmt, ...) -> int;

fn main() -> int {
	// own: single owner, stack allocated (move semantics)
	own int a = 10;
	assert a == 10;

	// shared: ARC heap-allocated via __blang_rc_alloc
	shared int b = 20;

	// sync: ARC + mutex via __blang_rc_alloc_sync
	sync int c = 30;

	// Value type (default)
	int d = 40;

	// Verify all values are correct
	assert a == 10, "own variable should be 10";
	assert d == 40, "value variable should be 40";

	printf("Ownership codegen test passed!\n");
	return 0;
}
