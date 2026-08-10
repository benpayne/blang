// modules-v2-graph U6a: a dependency's pub free function. Reaching it requires
// `import lib;` AND the qualified spelling `lib.greet(...)` — the flat merge into
// the global scope is retired, so the bare name no longer resolves.
pub fn greet(string name) -> string {
	return "hi " + name;
}
