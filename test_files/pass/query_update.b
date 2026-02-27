table struct User {
	int id;
	string name;
	bool active;
}

fn deactivate_user(int id) -> int {
	return update User
		|> where { .id == id }
		|> set { .active = false };
}

fn main() -> int {
	return 0;
}
