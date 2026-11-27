// modules-v2-lsp U3 negative fixture (converge T046): a dependency whose own
// compile fails, which this document does NOT import. The one setup diagnostic
// falls back to 1:1 and names the declaring blang.toml entry; the unrelated real
// error below is still published.
fn main() -> int {
	int x = "s";
	return x;
}
