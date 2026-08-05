// A string-interpolation placeholder whose base names nothing in scope must be
// REJECTED with a located diagnostic.
//
// It used to be copied into the output as literal source text, so this program
// compiled clean and printed "value={missing}" at runtime — a silent wrong
// answer with no diagnostic anywhere (known-issues KI-8).
fn main() -> int {
	string s = "value={missing}";
	println("{}", s);
	return 0;
}
