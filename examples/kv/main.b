// Example #5 — kv: a file-backed key-value store CLI.
//
//   kv set <key> <value>     store/overwrite a key
//   kv get <key>             print the value (exit 1 if missing)
//   kv has <key>             exit 0 if present, 1 if not
//   kv del <key>             remove a key (exit 1 if missing)
//   kv list                  print all key=value lines
//   kv keys                  print just the keys
//
// The store is a plain text file of key=value lines. Its path comes from
// --file=<path>, else the KV_FILE environment variable, else ./kv.store.
//
// This is the first real program written against the new stdlib breadth:
// sys.args, cli (flags + positionals), env (Option-returning lookup), and fs
// (exists/read_all/write_all). The store logic is pure string functions over
// the file content — no globals, no parallel-array state — so every piece is
// unit-testable with colocated `test` blocks (run them with `bcc test`).

import sys;
import cli;
import env;
import fs;

fn main() -> int {
	Array<string> args = sys.args();
	Array<string> pos = positionals(args);

	if pos.length == 0 {
		usage();
		return 2;
	}

	string cmd = pos[0];
	string path = store_path(args);

	if cmd == "list" {
		string content = load(path);
		Array<string> lines = split_lines(content);
		for line in lines {
			println("{}", line);
		}
		return 0;
	}

	if cmd == "keys" {
		string content = load(path);
		Array<string> lines = split_lines(content);
		for line in lines {
			println("{}", line_key(line));
		}
		return 0;
	}

	if pos.length < 2 {
		usage();
		return 2;
	}
	string key = pos[1];

	if cmd == "get" {
		Option<string> v = lookup(load(path), key);
		match v {
			some(s) {
				println("{}", s);
				return 0;
			}
			none {
				return 1;
			}
		}
	}

	if cmd == "has" {
		Option<string> v = lookup(load(path), key);
		match v {
			some(s) {
				return 0;
			}
			none {
				return 1;
			}
		}
	}

	if cmd == "del" {
		string content = load(path);
		Option<string> v = lookup(content, key);
		match v {
			some(s) {
				fs.write_all(path, del_entry(content, key));
				return 0;
			}
			none {
				println("kv: no such key: {}", key);
				return 1;
			}
		}
	}

	if cmd == "set" {
		if pos.length < 3 {
			usage();
			return 2;
		}
		string value = pos[2];
		fs.write_all(path, set_entry(load(path), key, value));
		return 0;
	}

	usage();
	return 2;
}

fn usage() {
	println("usage: kv [--file=<path>] <command>");
	println("  set <key> <value>   store/overwrite a key");
	println("  get <key>           print the value (exit 1 if missing)");
	println("  has <key>           exit 0 if present, 1 if not");
	println("  del <key>           remove a key (exit 1 if missing)");
	println("  list                print all key=value lines");
	println("  keys                print just the keys");
	println("store path: --file flag, else $KV_FILE, else ./kv.store");
}

// Where the store lives: --file=<path> flag, else $KV_FILE, else ./kv.store.
fn store_path(Array<string> args) -> string {
	string flag = flag_value(args, "file", "");
	if flag != "" {
		return flag;
	}
	return env.get_or("KV_FILE", "kv.store");
}

// Read the store, or "" when it does not exist yet.
fn load(string path) -> string {
	if fs.exists(path) {
		return fs.read_all(path);
	}
	return "";
}

// --- Pure store logic (unit-tested below) --------------------------------
//
// The store content is newline-separated `key=value` lines. Values may
// contain '='; keys may not (the first '=' is the separator).

// The key part of a `key=value` line ("" if the line has no '=').
fn line_key(string line) -> string {
	int eq = line.index_of("=");
	if eq < 0 {
		return "";
	}
	return line.substring(0, eq);
}

// The value part of a `key=value` line (text after the FIRST '=').
fn line_value(string line) -> string {
	int eq = line.index_of("=");
	if eq < 0 {
		return "";
	}
	return line.substring(eq + 1, line.length);
}

// Split content into non-empty lines.
fn split_lines(string content) -> Array<string> {
	Array<string> lines = [];
	string rest = content;
	while rest.length > 0 {
		int nl = rest.index_of("\n");
		if nl < 0 {
			lines.push(rest);
			rest = "";
		} else {
			if nl > 0 {
				lines.push(rest.substring(0, nl));
			}
			rest = rest.substring(nl + 1, rest.length);
		}
	}
	return lines;
}

// The value stored under `key`, or none.
fn lookup(string content, string key) -> Option<string> {
	Array<string> lines = split_lines(content);
	for line in lines {
		if line_key(line) == key {
			return Option.some(line_value(line));
		}
	}
	return Option.none;
}

// Content with `key` set to `value` (replacing an existing entry in place,
// appending otherwise).
fn set_entry(string content, string key, string value) -> string {
	string out = "";
	bool replaced = false;
	Array<string> lines = split_lines(content);
	for line in lines {
		if line_key(line) == key {
			out = out + key + "=" + value + "\n";
			replaced = true;
		} else {
			out = out + line + "\n";
		}
	}
	if !replaced {
		out = out + key + "=" + value + "\n";
	}
	return out;
}

// Content with `key`'s entry removed.
fn del_entry(string content, string key) -> string {
	string out = "";
	Array<string> lines = split_lines(content);
	for line in lines {
		if line_key(line) != key {
			out = out + line + "\n";
		}
	}
	return out;
}

// --- Tests (run with `bcc test`) ------------------------------------------

// Unwrap helpers, via match-as-expression (each arm yields a single value).
fn lookup_equals(string content, string key, string expected) -> bool {
	Option<string> v = lookup(content, key);
	return match v {
		some(s) { s == expected }
		none { false }
	};
}

fn lookup_is_none(string content, string key) -> bool {
	Option<string> v = lookup(content, key);
	return match v {
		some(s) { false }
		none { true }
	};
}

test "set then lookup" {
	string c = set_entry("", "name", "ada");
	assert lookup_equals(c, "name", "ada");
}

test "lookup missing key is none" {
	string c = set_entry("", "name", "ada");
	assert lookup_is_none(c, "email");
	assert lookup_is_none("", "anything");
}

test "set replaces existing entry" {
	string c = set_entry("", "name", "ada");
	c = set_entry(c, "name", "grace");
	assert lookup_equals(c, "name", "grace");
	Array<string> lines = split_lines(c);
	assert lines.length == 1, "replace must not duplicate the key";
}

test "entries are independent" {
	string c = set_entry("", "a", "1");
	c = set_entry(c, "b", "2");
	c = set_entry(c, "c", "3");
	assert lookup_equals(c, "a", "1");
	assert lookup_equals(c, "b", "2");
	assert lookup_equals(c, "c", "3");
}

test "del removes only that key" {
	string c = set_entry("", "a", "1");
	c = set_entry(c, "b", "2");
	c = del_entry(c, "a");
	assert lookup_is_none(c, "a");
	assert lookup_equals(c, "b", "2");
}

test "value may contain equals sign" {
	string c = set_entry("", "url", "http://x?a=1&b=2");
	assert lookup_equals(c, "url", "http://x?a=1&b=2");
}

test "line helpers split on first equals" {
	assert line_key("k=v") == "k";
	assert line_value("k=v") == "v";
	assert line_value("k=a=b") == "a=b";
	assert line_key("noequals") == "";
}

test "split_lines skips blanks and trailing newline" {
	Array<string> lines = split_lines("a=1\n\nb=2\n");
	assert lines.length == 2;
}
