import lib;

fn main() -> int {
	// Handle's init is private — this must say so, not "no constructor".
	Handle h = Handle(7);
	return h.id();
}
