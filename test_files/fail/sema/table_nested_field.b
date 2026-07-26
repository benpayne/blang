// A table struct field must have a SQL column mapping (primitive or string).
// A nested struct field would be silently left null by the row mapper — a
// guaranteed null-deref — so Sema rejects it, located, in all build modes.

struct Address {
	string city;
	string street;
}

table struct Customer {
	int id;
	string name;
	Address address;
}

fn main() -> int {
	return 0;
}
