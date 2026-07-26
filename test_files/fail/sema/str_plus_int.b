// Regression: string + non-string is a type error (not an implicit coercion,
// not an ICE). Use interpolation "k{i}" instead. Previously reached codegen as
// `add ptr, i32` and failed IR verification (compiler ICE).
fn main() -> int {
	int i = 7;
	string s = "k" + i;
	return 0;
}
