// Demo 8: JSON Roundtrip — Address Book
// Features: @json annotation, nested structs, struct literals, field access

@json
struct Contact {
	string name;
	string email;
	int age;
}

@json
struct AddressBook {
	string title;
	Contact primary;
	Contact secondary;
	int count;
}

fn main() -> int {
	println("=== JSON Address Book ===");
	println();

	// Create contacts
	Contact alice = Contact { name: "Alice Smith", email: "alice@example.com", age: 30 };
	Contact bob = Contact { name: "Bob Jones", email: "bob@example.com", age: 25 };

	// Serialize individual contacts
	println("Serializing contacts:");
	string alice_json = Contact_to_json(alice);
	println("  Alice: {}", alice_json);

	string bob_json = Contact_to_json(bob);
	println("  Bob:   {}", bob_json);

	// Roundtrip: deserialize Alice and verify fields
	Contact alice2 = Contact_from_json(alice_json);
	assert alice2.age == 30, "Alice age should survive roundtrip";
	println();
	println("Roundtrip check - Alice age: {} (expected 30)", alice2.age);

	// Create an address book with nested contacts
	AddressBook book = AddressBook {
		title: "My Contacts",
		primary: alice,
		secondary: bob,
		count: 2
	};

	println();
	print("Serialized AddressBook:\n  ");
	string book_json = AddressBook_to_json(book);
	println("{}", book_json);

	// Roundtrip the whole address book
	AddressBook book2 = AddressBook_from_json(book_json);

	println();
	println("Roundtrip verification:");
	println("  title: {}", book2.title);
	println("  count: {}", book2.count);
	println("  primary.name: {}", book2.primary.name);
	println("  primary.age: {}", book2.primary.age);
	println("  secondary.name: {}", book2.secondary.name);
	println("  secondary.age: {}", book2.secondary.age);

	assert book2.count == 2, "count should be 2";
	assert book2.primary.age == 30, "Alice age should be 30";
	assert book2.secondary.age == 25, "Bob age should be 25";

	println();
	println("JSON address book demo passed!");
	return 0;
}
