// Built-in Option<T> and Result<T,E> are available without any user definition.

fn lookup(int id) -> Option<int> {
	if id > 0 {
		return Option.some(id);
	}
	return Option.none;
}

fn parse(int n) -> Result<int, string> {
	if n < 0 {
		return Result.err("negative");
	}
	return Result.ok(n);
}

fn main() -> int {
	match lookup(5) {
		some(v) { }
		none { }
	}
	match parse(3) {
		ok(v) { }
		err(msg) { }
	}
	return 0;
}
