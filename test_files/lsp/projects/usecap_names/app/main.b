// modules-v2-lsp U4 (T041a) fixture: imports mid ONLY, never shapes, so Sq is
// held by use-capability alone (D7) and hover must qualify it by the defining
// module's human name.
import mid;

fn main() -> int {
	var s = mid.make(3);
	println("{}", s.area());
	return 0;
}
