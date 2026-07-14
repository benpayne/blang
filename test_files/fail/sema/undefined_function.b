// Call to a function that is not declared or imported is rejected with a located
// error naming the function (U3, FR-005).
fn main() -> int {
	return no_such_function();
}
