// LSP fixture: compiles cleanly — blangd must publish an empty diagnostics
// list for this document.
fn add(int a, int b) -> int {
	return a + b;
}

fn main() -> int {
	return add(1, 2);
}
