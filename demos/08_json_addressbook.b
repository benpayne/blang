// Demo 8: JSON Roundtrip — Address Book
// Features: @json annotation, nested structs, struct literals, field access

extern fn printf(cstring fmt, ...) -> int;
extern fn puts(cstring s) -> int;

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
	printf("=== JSON Address Book ===\n\n");

	// Create contacts
	Contact alice = Contact { name: "Alice Smith", email: "alice@example.com", age: 30 };
	Contact bob = Contact { name: "Bob Jones", email: "bob@example.com", age: 25 };

	// Serialize individual contacts
	printf("Serializing contacts:\n");
	string alice_json = Contact_to_json(alice);
	printf("  Alice: %s\n", alice_json.to_cstring());

	string bob_json = Contact_to_json(bob);
	printf("  Bob:   %s\n", bob_json.to_cstring());

	// Roundtrip: deserialize Alice and verify fields
	Contact alice2 = Contact_from_json(alice_json);
	assert alice2.age == 30, "Alice age should survive roundtrip";
	printf("\nRoundtrip check - Alice age: %d (expected 30)\n", alice2.age);

	// Create an address book with nested contacts
	AddressBook book = AddressBook {
		title: "My Contacts",
		primary: alice,
		secondary: bob,
		count: 2
	};

	printf("\nSerialized AddressBook:\n  ");
	string book_json = AddressBook_to_json(book);
	puts(book_json.to_cstring());

	// Roundtrip the whole address book
	AddressBook book2 = AddressBook_from_json(book_json);

	printf("\nRoundtrip verification:\n");
	printf("  title: %s\n", book2.title.to_cstring());
	printf("  count: %d\n", book2.count);
	printf("  primary.name: %s\n", book2.primary.name.to_cstring());
	printf("  primary.age: %d\n", book2.primary.age);
	printf("  secondary.name: %s\n", book2.secondary.name.to_cstring());
	printf("  secondary.age: %d\n", book2.secondary.age);

	assert book2.count == 2, "count should be 2";
	assert book2.primary.age == 30, "Alice age should be 30";
	assert book2.secondary.age == 25, "Bob age should be 25";

	printf("\nJSON address book demo passed!\n");
	return 0;
}
