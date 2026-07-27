// Recursive enums: a variant payload naming an enum is BOXED (stored as a
// pointer to a heap child), so self-referential sum types have finite layout
// and real ASTs are expressible. Covers: nested construction, multi-binding
// destructuring with recursion, subtree reuse from variables (payload
// retain), reassignment (old tree released), enum returns from functions
// (temp and variable sources), and a call-result enum passed straight into
// another call.
enum Expr {
	num(int),
	add(Expr, Expr),
	mul(Expr, Expr),
	neg(Expr)
}

fn eval(Expr e) -> int {
	return match e {
		num(n) { n }
		add(l, r) { eval(l) + eval(r) }
		mul(l, r) { eval(l) * eval(r) }
		neg(x) { 0 - eval(x) }
	};
}

fn make_tree(int a, int b) -> Expr {
	return Expr.add(Expr.num(a), Expr.num(b));
}

fn make_var(int a) -> Expr {
	Expr t = Expr.add(Expr.num(a), Expr.num(1));
	return t;
}

fn main() -> int {
	// (1 + 2) * 3 = 9
	Expr t = Expr.mul(Expr.add(Expr.num(1), Expr.num(2)), Expr.num(3));
	println("{}", eval(t));

	// subtree reuse from variables: (5 + 5) * -(5 + 5) = -100
	Expr five = Expr.num(5);
	Expr ten = Expr.add(five, five);
	Expr prod = Expr.mul(ten, Expr.neg(ten));
	println("{}", eval(prod));

	// reassignment chain: old trees released, result 4
	Expr d = Expr.num(1);
	d = Expr.add(d, Expr.num(1));
	d = Expr.add(d, Expr.num(1));
	d = Expr.add(d, Expr.num(1));
	println("{}", eval(d));

	// enum returns: construct-temp and variable sources
	println("{}", eval(make_tree(2, 3)));
	println("{}", eval(make_var(9)));
	return 0;
}
