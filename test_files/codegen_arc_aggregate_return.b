// ARC ledger #2 (closed): returning a multi-field struct whose Array fields
// were populated inside the function. The locals alias the returned struct's
// fields; ownership must transfer exactly once — previously this double-freed
// at cleanup (the reason stdlib `cli` returns scalars instead of a Flags
// struct). Covers: two aggregates alive at once, reading through both,
// mutating through a returned aggregate, and passing one to a function.

struct Flags {
	Array<string> names;
	Array<string> values;
	int count;
}

fn build(int n) -> Flags {
	Array<string> ns = [];
	Array<string> vs = [];
	for i in 0..n {
		ns.push("key{i}");
		vs.push("val{i}");
	}
	return Flags { names: ns, values: vs, count: n };
}

fn total_len(Flags f) -> int {
	return f.names.length + f.values.length;
}

fn main() -> int {
	Flags a = build(3);
	Flags b = build(2);
	println("{} {}", total_len(a), total_len(b));

	a.names.push("extra");
	println("{} {}", a.names.length, a.names[3]);
	println("{}={}", b.names[1], b.values[1]);
	return 0;
}
