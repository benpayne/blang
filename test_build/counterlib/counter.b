// U1 (construction ABI) fixture: a NON-GENERIC pub struct used across a .bmod
// boundary. Before the library-emitted factory, a consumer could reach into
// this struct's fields but could neither construct it (`Counter(5)` did not
// parse) nor call anything on it (`no method 'bump'`) — design record P8.
//
// The consumer must construct through __Counter_new, which this module emits;
// it never computes Counter's size and never generates its destructor.
pub struct Counter {
	int count;
	string label;
}

impl Counter {
	pub init(int start, string name) {
		self.count = start;
		self.label = name;
	}

	pub fn bump(self) -> int {
		self.count = self.count + 1;
		return self.count;
	}

	pub fn value(self) -> int {
		return self.count;
	}

	pub fn name(self) -> string {
		return self.label;
	}
}
