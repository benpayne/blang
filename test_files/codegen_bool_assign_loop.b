// Regression: assigning a bool literal (true/false, codegen'd as i32) to an i1
// bool variable must coerce to i1 before the store. Previously the assignment
// path emitted `store i32 into i1*` — a 4-byte write into a 1-byte stack slot —
// which corrupted an adjacent local (here the for-in loop counter), causing an
// infinite loop. The trigger required a bool flag reassigned inside a loop; the
// stack-layout-dependent corruption made it manifest only in some combinations.
fn main() -> int {
	Array<string> a = ["x", "y", "z"];
	bool found = false;
	int seen = 0;
	for it in a {
		seen = seen + 1;
		if it == "y" { found = true; }
	}
	// If the counter were clobbered by the bad store, this loop never terminates
	// and `seen` never reaches 3.
	assert seen == 3, "loop ran exactly 3 times";
	assert found, "found y";

	// Direct check that a bool reassigned from a literal holds the right value.
	bool flag = true;
	flag = false;
	assert !flag, "flag is false after reassignment";
	flag = true;
	assert flag, "flag is true after reassignment";

	println("PASS");
	return 0;
}
