// Seeded bug S1 (functional-hardening REQ-005): struct-valued field
// reassignment `o.inner = Inner { v: 99 }`. The store lands, but pre-fix the
// RHS struct temporary was released at end of statement, freeing the block the
// field points to — a read-path-dependent use-after-free. The FIRST read after
// reassignment could still see 99, but any intervening allocation (the
// println/interpolation format path) reused the freed block, so a SUBSEQUENT
// read returned stale data (1/0). TEETH: this test prints the reassigned field
// TWICE via println under a committed golden — pre-fix the second read is stale
// (fails the golden); post-fix both read 99. Also runs under --leak-check.
struct Inner { int v; }
struct Outer { Inner inner; }

fn main() -> int {
	Outer o = Outer { inner: Inner { v: 1 } };
	println("init  {}", o.inner.v);

	// Reassign the struct-valued field. Two consecutive interpolated reads:
	// the first println allocates a format string that, pre-fix, reused the
	// freed Inner block, so the second read returned stale data.
	o.inner = Inner { v: 99 };
	println("read1 {}", o.inner.v);
	println("read2 {}", o.inner.v);

	// A third read through a local (no intervening alloc) and the assert path.
	int x = o.inner.v;
	println("via-var {}", x);
	assert o.inner.v == 99, "struct-field reassignment reads back new value";

	// Reassign again to exercise release-of-old + take-new a second time.
	o.inner = Inner { v: 7 };
	println("again1 {}", o.inner.v);
	println("again2 {}", o.inner.v);
	assert o.inner.v == 7, "second reassignment";

	println("PASS");
	return 0;
}
