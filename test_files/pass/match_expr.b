// Value-producing match: in expression position each arm is `pattern { expr }`
// (a single expression, no semicolon) and the match yields the selected arm's
// value. Statement-position match keeps block-bodied arms.
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
	int doubled = 2 * match 3 { 3 { 21 } _ { 0 } };
	println("{} {}", area_ish(Shape.circle(4)), doubled);
	println("{}", describe(1));
	return 0;
}
