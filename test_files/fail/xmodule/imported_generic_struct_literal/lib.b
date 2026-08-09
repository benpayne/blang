// A generic pub struct with a `pub init`. Under U5b a struct literal for an
// imported generic type is a located error; construct with Pair<int>(...).
pub struct Pair<T> {
	T first;
	T second;
}

impl Pair {
	pub init(T first, T second) {
		self.first = first;
		self.second = second;
	}
}
