// Generic enum payloads holding ENUM values: Result<Ast, string> and
// Option<Ast>. The built-in Option/Result use a pointer-sized type-erased
// payload slot; an enum value (a struct, arbitrarily large) is BOXED into it
// at construction and unboxed at the match binding / `?` unwrap, with box
// ownership transferred to the unwrapped copy. Also covers the `?`-in-
// initializer error path (the pre-init variable slot is zeroed so the early
// return releases nothing).
// Result<T,E> / Option<T> with T = an enum (boxed generic payload)
enum Ast {
	num(int),
	add(Ast, Ast)
}

fn eval_ast(Ast a) -> int {
	return match a {
		num(n) { n }
		add(l, r) { eval_ast(l) + eval_ast(r) }
	};
}

fn parse_pair(int a, int b) -> Result<Ast, string> {
	if a < 0 {
		return Result.err("negative");
	}
	return Result.ok(Ast.add(Ast.num(a), Ast.num(b)));
}

fn wrap_var(int a) -> Option<Ast> {
	Ast t = Ast.add(Ast.num(a), Ast.num(1));
	return Option.some(t);
}

fn eval_or_zero(Result<Ast, string> r) -> int {
	match r {
		ok(t) { return eval_ast(t); }
		err(e) { return 0 - 1; }
	}
	return 0 - 2;
}

fn sum_two(int a, int b) -> Result<int, string> {
	Ast t = parse_pair(a, b)?;
	return Result.ok(eval_ast(t));
}

fn main() -> int {
	// match on a call-result Result<Ast, _> subject
	println("{}", eval_or_zero(parse_pair(3, 4)));
	println("{}", eval_or_zero(parse_pair(0 - 1, 4)));

	// ? unwrap of a boxed Ast payload
	match sum_two(10, 5) {
		ok(v) { println("{}", v); }
		err(e) { println("err {}", e); }
	}
	match sum_two(0 - 2, 5) {
		ok(v) { println("{}", v); }
		err(e) { println("err {}", e); }
	}

	// Option<Ast> built from a VARIABLE (payload-retain path on boxing)
	Option<Ast> o = wrap_var(6);
	match o {
		some(t) { println("{}", eval_ast(t)); }
		none { println("none"); }
	}
	return 0;
}
