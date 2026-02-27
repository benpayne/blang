table struct User {
	int id;
	string name;
	string email;
	bool active;
}

table struct Post {
	int id;
	int user_id;
	string title;
	string body;
}

fn main() -> int {
	return 0;
}
