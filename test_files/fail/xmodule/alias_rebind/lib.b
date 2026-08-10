// A dependency. A consumer may alias it with `import lib as l;` — after which the
// module is reachable ONLY through the alias `l`, never the original name `lib`.
pub fn greet(string name) -> string {
	return "hi " + name;
}
