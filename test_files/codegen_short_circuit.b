// Regression: && and || must short-circuit. If the right operand were always
// evaluated, `s[i]` with i out of range would abort with an index-out-of-bounds
// error (nonzero exit), so a clean exit 0 proves short-circuiting works.

fn main() -> int {
	string s = "ab";
	int i = 2; // out of range for "ab" (valid indices 0,1)

	int hits = 0;
	// Left is false -> right (s[i]) must NOT be evaluated.
	if i < 2 && s[i] == 'x' {
		hits = 1;
	}
	if hits != 0 {
		return 1;
	}

	// Left is true -> right (s[i]) must NOT be evaluated.
	if i >= 2 || s[i] == 'x' {
		hits = 2;
	}
	if hits != 2 {
		return 2;
	}

	// Sanity: normal (non-short-circuiting) evaluation still works.
	int a = 1;
	int b = 0;
	if a == 1 && b == 0 {
		return 0;
	}
	return 3;
}
