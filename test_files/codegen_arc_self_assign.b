// ARC matrix (functional-hardening U1 / REQ-001): self-assignment of a
// refcounted field must NOT double-free. `o.s = o.s` (string) and
// `o.inner = o.inner` (struct) load the same reference on both sides; the store
// must retain-before-release so the value survives (net refcount unchanged).
// Two consecutive reads after each self-assign guard against a UAF. Runs under
// --leak-check.
struct Inner  { int v; }
struct Holder { string s; Inner inner; }

fn main() -> int {
	Holder h = Holder { s: "keep", inner: Inner { v: 5 } };

	// String field self-assignment.
	h.s = h.s;
	println("s1 {}", h.s);
	println("s2 {}", h.s);
	assert h.s == "keep", "string self-assign survives";

	// Struct field self-assignment (retain-before-release ordering is critical).
	h.inner = h.inner;
	println("v1 {}", h.inner.v);
	println("v2 {}", h.inner.v);
	assert h.inner.v == 5, "struct self-assign survives";

	// Mutate through the field afterwards to prove it is still live.
	h.inner.v = 9;
	println("v3 {}", h.inner.v);
	assert h.inner.v == 9, "field live after self-assign";

	println("PASS");
	return 0;
}
