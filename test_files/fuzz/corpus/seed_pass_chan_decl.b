// Channel variable declaration with chan keyword
// Note: chan is a keyword but channels use regular type declarations for now

fn main() {
	int x = 0;
	spawn {
		x = 42;
	}
}
