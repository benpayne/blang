// The module IS imported, but the free function is used UNQUALIFIED. Post-U6a a
// dependency's functions resolve only through the import graph as `lib.greet`, so
// the bare `greet(...)` is a LOCATED error at the call site (enforcement). U6b
// sharpens the message to a D3-rendered "did you mean lib.greet?" diagnostic; U6a
// pins the contract that the rejection is located.
import lib;

fn main() -> int {
	println("{}", greet("world"));
	return 0;
}
