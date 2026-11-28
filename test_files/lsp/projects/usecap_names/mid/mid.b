// modules-v2-lsp U4 (T041a) fixture: the middle library imports shapes and
// returns its Sq, so a consumer can hold an Sq without ever naming shapes.
import shapes;

pub fn make(int n) -> Sq {
	return Sq(n);
}
