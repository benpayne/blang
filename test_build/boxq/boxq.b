// U5 spike (sharpest corner): the DEFINING module of a generic type Box<T>.
pub struct Box<T> { T v; }
impl Box {
	pub init(T x) { self.v = x; }
	pub fn get(self) -> T { return self.v; }
}
