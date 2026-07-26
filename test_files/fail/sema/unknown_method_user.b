// U3 diagnostic coverage: unknown method on a struct.
struct User { int id; }
impl User { init(int id) { self.id = id; } }
fn main() -> int {
	User u = User(1);
	return u.compute();
}
