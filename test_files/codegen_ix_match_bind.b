// Interaction matrix (functional-hardening U3 / REQ-003): match-bind into field
// AND method, and Option/Result unwrap -> field, across the some/none and ok/err
// arms. Printed AND asserted.

struct Point { int x; int y; }
impl Point { fn sum(self) -> int { return self.x + self.y; } }

fn find( int k ) -> Result<Point, int> {
	if k > 0 { return Result.ok(Point { x: k, y: 100 }); }
	return Result.err(0 - k);
}

fn lookup( int k ) -> Option<Point> {
	if k > 0 { return Option.some(Point { x: k, y: k + 1 }); }
	return Option.none;
}

fn main() -> int {
	// Option unwrap -> field, some arm.
	match lookup(3) {
		some(pt) {
			println("opt_field {}", pt.x);
			assert pt.x == 3, "opt field";
		}
		none { println("opt_none"); }
	}
	// Option none arm.
	match lookup(-1) {
		some(pt) { println("unexpected {}", pt.x); }
		none { println("opt_none_ok"); }
	}

	// match-bind -> method call.
	match lookup(4) {
		some(pt) {
			println("opt_method {}", pt.sum());
			assert pt.sum() == 9, "opt method sum";
		}
		none { }
	}

	// Result unwrap -> field, ok arm.
	match find(7) {
		ok(p) {
			println("res_field {}", p.x);
			println("res_method {}", p.sum());
			assert p.x == 7, "res field";
			assert p.sum() == 107, "res method";
		}
		err(e) { println("err {}", e); }
	}
	// Result err arm.
	match find(-5) {
		ok(p) { println("unexpected {}", p.x); }
		err(e) {
			println("res_err {}", e);
			assert e == 5, "res err value";
		}
	}

	println("PASS");
	return 0;
}
