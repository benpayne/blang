// modules-v2-exports U5b: the `Name<Args>(...)` generic construction spelling.
// A generic struct is now constructed via `pub init` monomorphized for the
// written type args — symmetric with the non-generic Counter(5) form, and the
// one external construction spelling for a generic struct now that imported
// struct literals are private. Covers value (int) and refcounted (string)
// type arguments; the string case exercises the field-assignment ARC retain in
// a monomorphized init (leak-clean under --leak-check).

struct Pair<T> { T first; T second; }

impl Pair {
	init(T a, T b) {
		self.first = a;
		self.second = b;
	}
	fn first(self) -> T { return self.first; }
	fn second(self) -> T { return self.second; }
}

fn main() -> int {
	Pair<int> p = Pair<int>(10, 32);
	println("int={} {}", p.first(), p.second());

	Pair<string> s = Pair<string>("ada ", "lovelace");
	println("str={}{}", s.first(), s.second());

	return 0;
}
