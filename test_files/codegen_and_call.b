// Regression: && / || with method-call (rvalue) operands must not ICE.
// A method call inside a short-circuit RHS creates refcounted string temps
// (its string argument); those must be released WITHIN the RHS block, not
// deferred to statement scope in the non-dominating merge block — previously
// "instruction does not dominate all uses" -> IR-verify ICE.
import collections;
fn main() -> int {
	Set<string> s = Set<string>();
	s.add("a");
	s.add("b");
	assert s.has("a") && s.has("b"), "both present";
	assert s.has("a") && !s.has("z"), "a and not z";
	bool r = s.has("z") && s.has("a");
	assert !r, "short-circuit: rhs skipped when lhs false";
	assert s.has("z") || s.has("a"), "or reaches rhs";
	println("PASS");
	return 0;
}
