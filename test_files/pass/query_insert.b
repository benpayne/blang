table struct User {
	int id;
	string name;
	string email;
	bool active;
}

fn create_user(string name, string email) -> int {
	return insert User { name: name, email: email, active: true };
}

fn main() -> int {
	return 0;
}
