// Regression: binding a string local from a borrowed source must retain.
// Before the fix, a plain variable copy (string b = a;) and an array-element
// bind (string s = args[i];) both double-freed at scope exit — the new local
// and the original owner (variable scope / array elem_dtor) released the same
// string. Also locks in Sema's for-in loop-variable typing: `line` below is
// typed string from Array<string>, so `out + line` concatenation type-checks.

fn join(Array<string> parts) -> string {
	string out = "";
	for part in parts {
		out = out + part + ",";
	}
	return out;
}

fn main() -> int {
	// Variable copy: two owners, both released.
	string a = "hello heap string";
	string b = a;
	println("{} / {}", a, b);

	// Array-element bind: local owner + array elem_dtor.
	Array<string> args = ["set", "name", "ada"];
	string cmd = args[0];
	string key = args[1];
	println("cmd={} key={}", cmd, key);

	// Element bind inside a loop body (the kv/cli pattern).
	int i = 0;
	while i < args.length {
		string cur = args[i];
		println("arg{}={}", i, cur);
		i = i + 1;
	}

	// For-in loop variable used in string concatenation.
	println("{}", join(args));

	return 0;
}
