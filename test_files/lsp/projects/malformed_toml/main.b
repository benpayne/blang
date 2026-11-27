// modules-v2-lsp U3 negative fixture: one unparseable blang.toml line. One
// Warning at 1:1 (bcc's own parse-error body), then the ordinary compile.
fn main() -> int {
	int x = "s";
	return x;
}
