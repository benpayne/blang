table struct User {
	int id;
	string name;
}

fn main() -> int {
	return insert User name: "test";
}
