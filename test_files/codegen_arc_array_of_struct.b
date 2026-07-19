// ARC matrix (functional-hardening U1 / REQ-001): an Array<struct> holding
// refcounted heap structs — push fresh temporaries (ownership transfers into the
// array), read element fields via index and via for-in, then the array is
// dropped (each element released exactly once). Runs under --leak-check.
struct Item { string name; int qty; }

fn main() -> int {
	Array<Item> items = [];
	items.push(Item { name: "apple", qty: 3 });
	items.push(Item { name: "pear", qty: 5 });
	items.push(Item { name: "plum", qty: 7 });
	println("len {}", items.length);

	// for-in over the aggregate, reading element fields.
	for it in items {
		println("item {} {}", it.name, it.qty);
	}

	// index reads (twice, guarding a stale read).
	println("idx0 {} {}", items[0].name, items[0].qty);
	println("idx2 {} {}", items[2].name, items[2].qty);
	println("idx0-again {} {}", items[0].name, items[0].qty);
	assert items[0].qty == 3, "elem 0 qty";
	assert items[2].name == "plum", "elem 2 name";

	println("PASS");
	return 0;
}
