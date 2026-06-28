// query T returns Array<T>: rows are mapped to heap structs (column i -> field i)
// and can be iterated with for-in and indexed. Exercised against in-memory SQLite.

table struct Todo {
	int id;
	string title;
	bool done;
}

extern fn __blang_db_exec_raw_default(cstring sql) -> int;

fn main() -> int {
	__blang_db_exec_raw_default(
		"CREATE TABLE todo (id INTEGER PRIMARY KEY, title TEXT, done INTEGER)" );

	insert Todo { id: 1, title: "buy milk", done: false };
	insert Todo { id: 2, title: "walk dog", done: true };
	insert Todo { id: 3, title: "write code", done: false };

	// query -> Array<Todo>
	Array<Todo> all = query Todo;
	if all.length != 3 { return 1; }

	// Indexing maps columns to fields correctly.
	Todo first = all[0];
	if first.id != 1 { return 2; }

	// for-in binds the loop variable to the element struct type.
	int seen = 0;
	for t in all {
		seen = seen + 1;
		if t.id == 2 {
			if t.done == false { return 3; }   // row 2 is done
		}
	}
	if seen != 3 { return 4; }

	// Filtered query with a bound WHERE parameter.
	int target = 1;
	Array<Todo> done_zero = query Todo |> where { .done == target };
	if done_zero.length != 1 { return 5; }   // only id=2 has done=1
	Todo d = done_zero[0];
	if d.id != 2 { return 6; }

	return 0;
}
