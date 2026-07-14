// U3/U4 diagnostic coverage: unknown field on a different struct type.
struct User { int id; string name; }
impl User { init(int id, string name) { self.id = id; self.name = name; } }
fn main() -> int {
	User u = User(1, "a");
	return u.age;
}
