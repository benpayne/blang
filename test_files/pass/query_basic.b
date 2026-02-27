table struct User {
	int id;
	string name;
	bool active;
}

fn get_users() -> int {
	return query User
		|> where { .active == true }
		|> order_by { .name }
		|> limit(100);
}

fn main() -> int {
	return 0;
}
