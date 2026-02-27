table struct User {
	int id;
	string name;
}

table struct Post {
	int id;
	int user_id;
	string title;
}

fn user_with_posts(int id) -> int {
	return query User
		|> where { .id == id }
		|> join Post on { .id == .user_id }
		|> first;
}

fn main() -> int {
	return 0;
}
