// Match expression with missing braces should fail

fn test(int x) -> int {
	match x {
		1 => return 0;
	}
}
