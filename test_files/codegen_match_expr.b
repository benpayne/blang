// Value-producing match (expression position): each arm is `pattern { expr }`.
// Covers: enum subject with payload bindings, int subject with wildcard,
// built-in Option unwrap, string results (borrowed binding + literal arms),
// direct use inside a larger expression, and match as a return value.
enum Shape { circle(int), square(int), empty }

fn area_ish(Shape s) -> int {
	int a = match s {
		circle(r) { r * 3 }
		square(w) { w * w }
		empty { 0 }
	};
	return a;
}

fn describe(int n) -> string {
	return match n {
		0 { "zero" }
		1 { "one" }
		_ { "many" }
	};
}

fn unwrap_or(Option<string> o, string dflt) -> string {
	return match o {
		some(v) { v }
		none { dflt }
	};
}

fn main() -> int {
	println("{}", area_ish(Shape.circle(4)));
	println("{}", area_ish(Shape.square(5)));
	println("{}", area_ish(Shape.empty));

	println("{}", describe(0));
	println("{}", describe(1));
	println("{}", describe(7));

	// string results through a variable-held Option (payload released at scope exit)
	Option<string> a = Option.some("hi");
	Option<string> b = Option.none;
	string fb = "fallback";
	println("{}", unwrap_or(a, fb));
	println("{}", unwrap_or(b, fb));

	// direct use inside a larger expression
	int doubled = 2 * match 3 { 3 { 21 } _ { 0 } };
	println("{}", doubled);

	// bound string result, used after
	string d = match 2 { 2 { "two" } _ { "other" } };
	println("{} {}", d, d.length);
	return 0;
}
