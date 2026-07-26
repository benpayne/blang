// Demo 8: JSON Roundtrip — Address Book
// Features: @json annotation, nested structs, struct literals, field access,
//           file I/O (write the JSON to disk and read it back)

import fs;

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

	// --- Persist to a file and read it back (file I/O) ---
	string path = "/tmp/blang_addressbook_demo.json";
	println();
	println("Writing JSON to {} ...", path);
	fs.write_all(path, book_json);

	string loaded = fs.read_all(path);
	assert loaded == book_json, "file roundtrip: bytes on disk should match";

	// Parse the JSON that came back off disk and verify nested fields survived
	AddressBook book3 = AddressBook_from_json(loaded);
	println("Read back and parsed from disk:");
	println("  title: {}", book3.title);
	println("  primary.name: {}", book3.primary.name);
	println("  primary.age: {}", book3.primary.age);
	println("  secondary.email: {}", book3.secondary.email);

	assert book3.title == "My Contacts", "file roundtrip: title";
	assert book3.primary.age == 30, "file roundtrip: Alice age";
	assert book3.primary.name == "Alice Smith", "file roundtrip: Alice name";
	assert book3.secondary.email == "bob@example.com", "file roundtrip: nested email";

	// Clean up the temp file
	fs.remove(path);
	assert fs.exists(path) == false, "file should be removed";

	println();
	println("JSON address book demo passed (with file I/O)!");
	return 0;
}
