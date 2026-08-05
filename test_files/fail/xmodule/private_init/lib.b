// A struct whose `init` is private: constructible only inside its own module
// (design record — "a factory-only or handle-style type, with no extra
// mechanism"). The interface still DECLARES the init, without `pub`, so a
// consumer can be told the constructor is private rather than absent.
pub struct Handle {
	int id;
}

impl Handle {
	init(int v) {
		self.id = v;
	}

	pub fn id(self) -> int {
		return self.id;
	}
}

pub fn make(int v) -> Handle {
	return Handle(v);
}
