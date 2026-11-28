// modules-v2-lsp U2 stdlib-discovery goldens: zero imports, prelude types only.
// Clean in the stdlib/prelude context `bcc file.b` uses; single-file (no
// stdlib) it reports `unknown type 'Map'` — the fallback signal.
fn main() -> int {
	Map<string, int> m = Map<string, int>();
	m.set("a", 1);
	Set<int> s = Set<int>();
	s.add(2);
	Buffer b = Buffer(8);
	b.append_byte(65);
	return m.length() + s.length() + b.get_length();
}
