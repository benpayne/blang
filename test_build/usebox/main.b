// U5 spike: the CONSUMER. Imports midx only — NEVER boxq. Calls a pub fn that
// returns a foreign generic Box<int> (D7 use-capability across an un-named
// module), then calls its pub method .get() and monomorphizes Box<int> from the
// body shipped transitively via midx.bmod -> boxq.bmod.
// modules-v2-graph U6a: midx's free function get_box is reached QUALIFIED
// (midx.get_box) after `import midx;`. `Box<int>` is still named unqualified here
// — naming a foreign type is D7 name-capability, whose enforcement (usebox would
// then `import boxq;` or infer) lands in U6b; U6a keeps the type namable as the
// bridge. The method call b.get() is use-capability (no import needed).
import midx;
fn main() -> int {
	Box<int> b = midx.get_box(7);
	println("{}", b.get());
	return 0;
}
