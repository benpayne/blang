// modules-v2-lsp U2 stdlib_didchange_import golden: uses timer.after WITHOUT
// `import timer;` — the import-gated module is not in the context, so the use
// is an error; the golden then adds the import via didChange.
fn main() -> int {
	int t = timer.after(10);
	return t;
}
