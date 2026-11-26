// KG-8 regression: a DIAMOND dependency graph (diamondapp -> midx -> boxq and
// diamondapp -> boxq). boxq is reached by two paths — not a cycle — so bcc must
// build it once and dedup it. This is also the transitive D7 name-capability
// positive through bcc: diamondapp imports boxq, so it may NAME Box<int>.
import midx;
import boxq;
fn main() -> int {
	Box<int> b = midx.get_box(7);
	Box<int> c = Box<int>(5);
	println("{}", b.get() + c.get());
	return 0;
}
