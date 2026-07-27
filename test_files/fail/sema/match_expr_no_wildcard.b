// EXPECT-ERROR: match used as an expression on a non-enum subject must have a wildcard
fn main() -> int {
	int x = match 5 {
		1 { 10 }
		2 { 20 }
	};
	return x;
}
