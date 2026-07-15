// bcc test fixture: contains a failing test.
//
// The failing assert prints a located `<file>:<line>:` diagnostic (test mode),
// and the run must exit non-zero. A passing test appears BOTH before and after
// the failing one to prove isolation — a failed test does not abort its
// siblings (fork-per-test).

fn add(int a, int b) -> int {
	return a + b;
}

test "passes_before" {
	assert add(1, 1) == 2;
}

test "fails_here" {
	assert add(2, 2) == 5;
}

test "passes_after" {
	assert add(3, 3) == 6;
}
