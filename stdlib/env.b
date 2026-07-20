// stdlib/env.b — Env module: read process environment variables.
//
// Usage: import env;
//   env.get(name)          -> Option<string>  (some(value) if set, else none)
//   env.get_or(name, dflt) -> string          (value if set, else dflt)
//   env.has(name)          -> bool
//
// __blang_env_get returns a freshly-created (owned, +1) string; it is only
// called here when has() is true, so it never yields a null string (the wrapper
// never exposes the raw C null — none is produced structurally via Option).

extern fn __blang_env_get(string name) -> string;
extern fn __blang_env_has(string name) -> bool;

pub fn has(string name) -> bool {
	return __blang_env_has(name);
}

pub fn get(string name) -> Option<string> {
	if __blang_env_has(name) {
		return Option.some(__blang_env_get(name));
	}
	return Option.none;
}

pub fn get_or(string name, string fallback) -> string {
	if __blang_env_has(name) {
		return __blang_env_get(name);
	}
	return fallback;
}
