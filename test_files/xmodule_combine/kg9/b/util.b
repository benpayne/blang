// KG-9 fixture: a DIFFERENT module that shares a/util.b's basename. Reaching
// into Secret's private field must still be a field-privacy error (module
// identity is the canonical path, not the basename).
import util;
fn main() -> int {
	var s = util.make(4);
	return s.n;
}
