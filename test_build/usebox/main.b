// U5 spike: the CONSUMER. Imports midx only — NEVER boxq. Calls a pub fn that
// returns a foreign generic Box<int> (D7 use-capability across an un-named
// module), then calls its pub method .get() and monomorphizes Box<int> from the
// body shipped transitively via midx.bmod -> boxq.bmod.
import midx;
fn main() -> int {
	Box<int> b = get_box(7);
	println("{}", b.get());
	return 0;
}
