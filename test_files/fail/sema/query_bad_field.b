// Field validation (task 150): referencing a column that does not exist on the
// table struct is a compile error.

table struct User {
	int id;
	string name;
}

fn main() -> int {
	int t = 5;
	return delete User |> where { .nonexistent == t };
}
