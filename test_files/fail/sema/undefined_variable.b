// Reference to a variable that was never declared is rejected with a located
// error naming the variable (U3, FR-004; resolved eagerly by the parser and
// routed through the single DiagnosticEngine).
fn main() -> int {
	return missing_var;
}
