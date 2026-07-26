// Regression: chained (multi-level) struct field access `a.b.c` must read
// correctly in ALL contexts — variable assignment, println args, and
// comparisons. Previously it returned empty/zero in value contexts (a
// FieldAccessExpression object could not resolve its struct type in
// genFieldAccess) while comparisons happened to evaluate; the fix resolves the
// object's struct type from the Sema-annotated resolved type. Golden output is
// load-bearing here: the asserts alone passed while the printed values were
// empty, so only a stdout golden catches the display half of the bug.
struct Inner { string name; int age; }
struct Mid   { Inner inner; int m; }
struct Outer { Mid mid; int n; }

fn main() -> int {
	Inner i = Inner { name: "Alice", age: 30 };
	Mid mid = Mid { inner: i, m: 7 };
	Outer o = Outer { mid: mid, n: 99 };

	// 3-level chained read into locals, then print
	string nm = o.mid.inner.name;
	int ag = o.mid.inner.age;
	println("via var  : name={} age={}", nm, ag);

	// 3-level chained read directly as println args
	println("direct   : name={} age={}", o.mid.inner.name, o.mid.inner.age);

	// intermediate levels
	println("levels   : mid.m={} n={}", o.mid.m, o.n);

	// comparisons (these passed even with the bug — kept as a guard)
	assert o.mid.inner.name == "Alice", "3-level name";
	assert o.mid.inner.age == 30, "3-level age";
	assert o.mid.m == 7, "2-level m";
	assert o.n == 99, "1-level n";

	// chained WRITE through nested fields (was silently dropped)
	o.mid.inner.age = 42;
	o.mid.inner.name = "Bob";
	o.mid.m = 8;
	println("wrote    : name={} age={} m={}", o.mid.inner.name, o.mid.inner.age, o.mid.m);
	assert o.mid.inner.age == 42, "write 3-level age";
	assert o.mid.inner.name == "Bob", "write 3-level name";
	assert o.mid.m == 8, "write 2-level m";

	println("PASS");
	return 0;
}
