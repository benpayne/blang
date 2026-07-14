table struct User {
	int id;
	string name;
	bool active;
}

fn get_users() -> int {
	var users = query User
		|> where { .active == true }
		|> order_by { .name }
		|> limit(100);
	return 0;
}

fn main() -> int {
	return 0;
}
