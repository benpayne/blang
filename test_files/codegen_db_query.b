// End-to-end database test (task 156): parse -> SQL -> execute against SQLite.
// Exercises the default connection, runtime parameter binding (insert values,
// update SET + WHERE, delete WHERE), and real SQL execution.
//
// Run via test_codegen.sh, which links libblang_db + sqlite3 and points
// BLANG_DATABASE_URL at an in-memory database.

table struct User {
	int id;
	string name;
	bool active;
}

// Create the backing table on the default connection (migrations do this in a
// real project; here we set it up directly so the test is self-contained).
extern fn __blang_db_exec_raw_default(cstring sql) -> int;

fn main() -> int {
	__blang_db_exec_raw_default(
		"CREATE TABLE user (id INTEGER PRIMARY KEY, name TEXT, active INTEGER)" );

	// Inserts bind their field values as parameters; each affects one row.
	int a = insert User { id: 1, name: "alice", active: true };
	int b = insert User { id: 2, name: "bob", active: false };
	int c = insert User { id: 3, name: "carol", active: true };

	// Update with a bound WHERE parameter and a bound SET value.
	int target = 2;
	string newname = "robert";
	int updated = update User |> where { .id == target } |> set { .name = newname };

	// Run a SELECT with a bound WHERE parameter (exercises the query path).
	int active_flag = 1;
	query User |> where { .active == active_flag };

	// Delete with a bound WHERE parameter.
	int delid = 3;
	int deleted = delete User |> where { .id == delid };

	if a == 1 && b == 1 && c == 1 && updated == 1 && deleted == 1 {
		return 0;
	}
	return 1;
}
