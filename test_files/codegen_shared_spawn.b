// E2E test: shared variable readable in spawn with RC retain/release
// Tests: shared var captured in spawn, RC properly retained across threads

fn main() -> int {
	shared int value = 42;

	// shared is immutable — can be read in spawn but not assigned
	spawn {
		int local = value;
	}

	println("Shared spawn codegen test passed!");
	return 0;
}
