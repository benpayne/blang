import counterlib;

fn main() -> int {
	// Construction across a .bmod boundary: lowers to a call to the factory
	// the library emitted (__Counter_new), NOT to a local
	// __blang_rc_alloc_dtor + Counter_init pair.
	Counter c = Counter(5, "hits");

	println("start = {}", c.value());
	println("bump = {}", c.bump());
	println("bump = {}", c.bump());
	println("label = {}", c.name());

	// A refcounted field (string) proves the destructor the LIBRARY installed
	// runs on release: the consumer never generated one.
	Counter d = Counter(100, "other");
	println("other = {} {}", d.value(), d.name());

	return 0;
}
