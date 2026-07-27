// Multi-binding enum destructuring: pattern(a, b, ...) binds one variable per
// variant associated type, extracted at sequential payload offsets (mirroring
// construction). Covers 2- and 3-payload variants, mixed string+int payloads,
// statement and expression form, and a construct used directly as the subject
// (which requires Sema to annotate EnumConstructExpression with its enum type).
enum Msg {
	move(int, int),
	write(string),
	color(int, int, int),
	quit
}

enum Entry { kv(string, int), flag(bool) }

fn describe(Msg m) -> string {
	return match m {
		move(x, y) { "move {x},{y}" }
		write(s) { s }
		color(r, g, b) { "rgb {r} {g} {b}" }
		quit { "quit" }
	};
}

fn main() -> int {
	println("{}", describe(Msg.move(3, 4)));
	println("{}", describe(Msg.write("hello")));
	println("{}", describe(Msg.color(255, 128, 0)));
	println("{}", describe(Msg.quit));

	match Msg.move(7, 9) {
		move(a, b) { println("{}", a * b); }
		_ { println("no"); }
	}

	// string + int in one variant, construct as direct subject, methods on bindings
	match Entry.kv("age", 41) {
		kv(k, v) { println("{}={}", k, v); }
		_ { println("no"); }
	}
	int x = match Entry.kv("n", 5) { kv(k, v) { v + k.length } _ { 0 } };
	println("{}", x);
	return 0;
}
