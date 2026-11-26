// modules-v2-graph U1 done-condition 1: a library exporting a generic Box<T>.
// Box<int> is instantiated INSIDE this library (the consumer never names Box),
// so boxa's .a carries its own linkonce_odr Box_m<digestA>_int_* symbols.
// A second internal user of Box<int> (boxed_twice) exercises SAME-ORIGIN dedup:
// the Box<int> instance is monomorphized once and shared, not duplicated.
pub struct Box<T> { T value; }

impl Box {
	pub init(T v) { self.value = v; }
	pub fn get(self) -> int { return self.value; }
}

pub fn boxed_a(int v) -> int {
	Box<int> b = Box<int>(v);
	return b.get() + 100;
}

pub fn boxed_twice(int v) -> int {
	Box<int> b1 = Box<int>(v);
	Box<int> b2 = Box<int>(b1.get());
	return b2.get();
}

// KG-1: a same-named generic FUNCTION in both boxa and boxb, with DIFFERENT
// bodies, each instantiated with int inside its own library. With function
// identity mangling the two tag<int> instances are distinct symbols; without it
// they collapse onto one linkonce_odr `tag_int` and one library runs the other's code.
fn tag<T>(T v) -> int { return 1; }
pub fn tagged_a(int v) -> int { return tag(v); }
