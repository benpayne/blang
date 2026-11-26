// KG-9 fixture: a module whose BASENAME (util) collides with the consumer's.
pub struct Secret { int n; }
impl Secret {
	pub init(int v) { self.n = v; }
	pub fn get(self) -> int { return self.n; }
}
pub fn make(int v) -> Secret { return Secret(v); }
