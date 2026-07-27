// EXPECT-ERROR: match arms have incompatible types
fn main() -> int {
	int x = match 1 {
		1 { 10 }
		_ { "oops" }
	};
	return x;
}
