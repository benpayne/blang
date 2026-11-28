// modules-v2-lsp U3 negative fixture: an unpinned git dependency (bcc's own
// hard error). One setup diagnostic at the import, bcc's exact text.
import greet;

fn main() -> int {
	int v = greet.hi();
	int x = "s";
	return v + x;
}
