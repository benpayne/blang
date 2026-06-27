// Passing bool values as function arguments. `true`/`false` literals are
// generated as i32 constants but a `bool` parameter is i1, so the call site must
// narrow the argument to i1 (previously this produced an LLVM type mismatch).
// Covers literal args and a bool-typed variable arg. Exits 0 on success.

fn pick(bool flag, int yes, int no) -> int {
	if flag {
		return yes;
	}
	return no;
}

fn main() -> int {
	// Literal arguments.
	int a = pick(true, 10, 20);    // -> 10
	int b = pick(false, 10, 20);   // -> 20

	// Bool variable argument.
	bool v = true;
	int c = pick(v, 1, 2);         // -> 1

	if a == 10 && b == 20 && c == 1 {
		return 0;
	}
	return 1;
}
