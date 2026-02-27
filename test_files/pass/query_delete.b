table struct Post {
	int id;
	int user_id;
	string title;
}

fn remove_posts(int user_id) -> int {
	return delete Post
		|> where { .user_id == user_id };
}

fn main() -> int {
	return 0;
}
