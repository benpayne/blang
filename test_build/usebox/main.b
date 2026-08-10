// U5 spike: the CONSUMER. Imports midx only — NEVER boxq. Calls a pub fn that
// returns a foreign generic Box<int> (D7 use-capability across an un-named
// module), then calls its pub method .get() and monomorphizes Box<int> from the
// body shipped transitively via midx.bmod -> boxq.bmod.
// modules-v2-graph U6a/U6b: midx's free function get_box is reached QUALIFIED
// (midx.get_box) after `import midx;`. The returned value is a foreign generic
// Box<int> owned by boxq, which usebox NEVER imports — so it must not NAME Box
// (D7 name-capability requires the import). `var` inference keeps this pure
// use-capability: usebox holds the value and calls its pub method .get() without
// ever naming Box. Box<int> is still monomorphized + linked from the body shipped
// transitively via midx.bmod -> boxq.bmod (CodeGen's mStructDefMap).
import midx;
fn main() -> int {
	var b = midx.get_box(7);
	println("{}", b.get());
	return 0;
}
