// modules-v2-graph U1 done-condition 1: a SECOND library, distinct origin, that
// also exports a same-named generic Box<T> and instantiates Box<int> internally.
// Without identity-in-mangling both libraries' Box<int> symbols would collapse
// onto one linkonce_odr symbol at link (P10). With U1 they are distinct.
pub struct Box<T> { T value; }

impl Box {
	pub init(T v) { self.value = v; }
	pub fn get(self) -> int { return self.value; }
}

pub fn boxed_b(int v) -> int {
	Box<int> b = Box<int>(v);
	return b.get() + 200;
}

// KG-1: see boxa — same name, different body.
fn tag<T>(T v) -> int { return 2; }
pub fn tagged_b(int v) -> int { return tag(v); }
