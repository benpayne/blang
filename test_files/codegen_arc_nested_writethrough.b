// ARC matrix (functional-hardening U1 / REQ-001): 2-level struct nesting with
// write-through of a leaf field AND reassignment of an intermediate refcounted
// struct field (`o.mid.inner = Inner{...}` — the S1 fix path, but where the
// assignment object is itself a chained field access). Two consecutive
// interpolated reads after the reassignment guard the read-path-dependent UAF.
// Runs under --leak-check.
struct Inner { int v; string tag; }
struct Mid   { Inner inner; int m; }
struct Outer { Mid mid; }

fn main() -> int {
	Outer o = Outer { mid: Mid { inner: Inner { v: 1, tag: "a" }, m: 10 } };
	println("leaf {}", o.mid.inner.v);
	println("m {}", o.mid.m);

	// Write-through a leaf field.
	o.mid.inner.v = 5;
	println("leaf2 {}", o.mid.inner.v);
	assert o.mid.inner.v == 5, "leaf write-through";

	// Reassign the intermediate refcounted struct field. Old Inner released,
	// new Inner owned by the field — verified by two consecutive reads.
	o.mid.inner = Inner { v: 77, tag: "b" };
	println("after {} {}", o.mid.inner.v, o.mid.inner.tag);
	println("after2 {} {}", o.mid.inner.v, o.mid.inner.tag);
	assert o.mid.inner.v == 77, "intermediate reassign v";
	assert o.mid.inner.tag == "b", "intermediate reassign tag";

	println("PASS");
	return 0;
}
