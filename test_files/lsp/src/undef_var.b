// LSP fixture: references an undefined variable — blangd must publish one
// located error diagnostic for this document.
fn main() -> int {
	return missing;
}
