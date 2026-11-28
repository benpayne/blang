// modules-v2-lsp U3 negative fixture: a pinned git dependency with no checkout
// in the cache. blangd never fetches: one "not fetched" diagnostic at the import.
import greet;

fn main() -> int {
	int v = greet.hi();
	int x = "s";
	return v + x;
}
