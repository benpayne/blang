// A channel carries elements by raw byte copy (__blang_chan_send/recv), so a
// refcounted heap element (string/Array/Buffer/struct) would be transferred
// without a reference count and dangle. Sema rejects a refcounted channel
// element type in all build modes (reject, don't coerce). Value-type channels
// (chan<int>, chan<long>, chan<bool>, ...) are supported.

fn main() -> int {
	chan<string> ch;
	ch.send("hello");
	return 0;
}
