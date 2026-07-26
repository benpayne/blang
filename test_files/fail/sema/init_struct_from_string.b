// U4 diagnostic coverage: incompatible initializer (struct from string).
struct Box { int v; }
impl Box { init(int v) { self.v = v; } }
fn main() -> int {
	Box b = "hi";
	return 0;
}
