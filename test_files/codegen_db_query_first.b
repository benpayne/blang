// `query T |> ... |> first` returns Option<T>: some(row 0) when a row matches,
// none when nothing does. Previously this miscompiled (the Array result pointer
// was stored raw into the Option and freed). Exercised against in-memory SQLite.

table struct Todo {
	int id;
	string title;
	bool done;
}

extern fn __blang_db_exec_raw_default(cstring sql) -> int;

fn main() -> int {
	__blang_db_exec_raw_default(
		"CREATE TABLE todo (id INTEGER PRIMARY KEY, title TEXT, done INTEGER)" );

	insert Todo { id: 1, title: "write tests", done: false };
	insert Todo { id: 2, title: "ship it", done: true };

	// Hit: row exists — payload fields must map correctly.
	int want = 2;
	Option<Todo> hit = query Todo |> where { .id == want } |> first;
	match hit {
		some(t) {
			println("found: {} (done={})", t.title, t.done);
		}
		none {
			println("missing row 2");
			return 1;
		}
	}

	// Miss: no such row — must be none, not garbage.
	int nope = 99;
	Option<Todo> miss = query Todo |> where { .id == nope } |> first;
	match miss {
		some(t) {
			println("phantom row: {}", t.title);
			return 2;
		}
		none {
			println("id 99 not found (correct)");
		}
	}

	// first without a where: LIMIT 1 over the whole table.
	Option<Todo> any = query Todo |> order_by { .id } |> first;
	match any {
		some(t) {
			println("first by id: {}", t.title);
		}
		none {
			return 3;
		}
	}

	return 0;
}
