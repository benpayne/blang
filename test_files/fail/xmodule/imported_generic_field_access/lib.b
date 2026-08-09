// A generic pub struct. Under U5b its fields are private across a .bmod
// boundary too (visibility is Sema-only for generics — the .bmod still ships
// full layout + all bodies for consumer monomorphization, A6). Read via the
// accessor method; construct via `pub init`.
pub struct Pair<T> {
	T first;
	T second;
}

impl Pair {
	pub init(T first, T second) {
		self.first = first;
		self.second = second;
	}

	fn first(self) -> T {
		return self.first;
	}
}
