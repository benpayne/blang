// E2E test: shared variable readable in spawn with RC retain/release
// Tests: shared var captured in spawn, RC properly retained across threads

extern fn printf(string fmt, ...) -> int;

fn main() -> int {
	shared int value = 42;

	// shared is immutable — can be read in spawn but not assigned
	spawn {
		int local = value;
	}

	printf("Shared spawn codegen test passed!\n");
	return 0;
}
