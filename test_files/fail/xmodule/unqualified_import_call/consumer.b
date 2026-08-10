// The module IS imported, but the free function is used UNQUALIFIED. A
// dependency's functions resolve only through the import graph as `lib.greet`, so
// the bare `greet(...)` is a LOCATED error at the call site (U6a enforcement).
// U6b-2 sharpens the message to the D3-rendered "did you mean 'lib.greet'?" form.
import lib;

fn main() -> int {
	string r = greet("world");
	return 0;
}
