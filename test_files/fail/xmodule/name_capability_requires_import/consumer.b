// NEGATIVE, name-capability direction: the consumer NAMES a foreign type
// (declares `Widget w` and constructs `Widget(5)`) WITHOUT `import lib;`. Under
// D7 that is a located error — name-capability requires the import. (Contrast the
// use-capability positive `test_build/usebox`, which holds a foreign value via
// `var` and calls its pub method with no import of the owner.) U6b-2 sharpens the
// message to a D3 "did you mean to import lib?"; U6b-1 pins the located rejection.
fn main() -> int {
	Widget w = Widget(5);
	return w.size_of();
}
