// ARC matrix (functional-hardening U1 / REQ-001): a refcounted `string` stored
// into a struct field, read back, REASSIGNED (old string must be released, not
// leaked, and the new one held with a counted reference), and assigned from an
// existing owner (local var — must retain so the local stays valid). Two
// consecutive interpolated reads after each write guard against a stale/UAF
// read. Runs under --leak-check with 0 leaks.
struct Holder { string s; int n; }

fn main() -> int {
	Holder h = Holder { s: "hello", n: 1 };
	println("f1 {}", h.s);

	// Reassign the string field from a literal (fresh temporary).
	h.s = "world";
	println("f2 {}", h.s);
	println("f3 {}", h.s);
	assert h.s == "world", "string field reassign (literal)";

	// Assign from an existing owner: the local must remain valid afterwards
	// (retain-on-store), and the old field value released.
	string local = "greetings";
	h.s = local;
	println("f4 {}", h.s);
	println("local {}", local);
	assert h.s == "greetings", "string field reassign (owner)";
	assert local == "greetings", "source local survives";

	println("PASS");
	return 0;
}
