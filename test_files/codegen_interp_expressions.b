// Regression: which expressions a string interpolation can actually render.
//
// Three wrong answers used to live here, all silent (known-issues KI-8, KI-10):
//
//   1. a DOTTED placeholder ("{p.x}") was copied to the output as literal
//      SOURCE TEXT, so a program printed "{p.x}" instead of the field value;
//   2. a bare STRUCT placeholder ("{s}") was handed straight to
//      __blang_string_concat_many as if the struct pointer were a BlangString —
//      a type confusion at the C boundary that rendered as empty;
//   3. `self` as a print argument fell through the same hole, because the
//      implicit self parameter's declared type name is the literal "self" and
//      so failed every "is this a struct?" test.
//
// The rule this locks in: an interpolation placeholder is RESOLVED (through
// Printable for a struct, exactly as the `{}` format placeholder does) or
// REJECTED — it is never emitted as source text and never silently dropped.
struct Inner {
	int depth;
	string label;
}

impl Inner {
	init(int d, string l) {
		self.depth = d;
		self.label = l;
	}
}

struct Outer {
	int id;
	string name;
	Inner inner;
}

impl Outer {
	init(int i, string n, Inner nested) {
		self.id = i;
		self.name = n;
		self.inner = nested;
	}

	// A dotted path rooted at `self`, both an int and a refcounted string field.
	fn describe(self) -> string {
		return "Outer(id={self.id},name={self.name})";
	}

	// `self` as a whole, as a print argument: must dispatch through Printable.
	fn show_self(self) {
		println("self-arg = {}", self);
	}
}

impl Printable for Outer {
	fn to_string(self) -> string {
		return "Outer#{self.id}";
	}
}

struct Plain {
	int v;
}

impl Plain {
	init(int a) { self.v = a; }
}

impl Printable for Plain {
	fn to_string(self) -> string {
		return "Plain({self.v})";
	}
}

fn main() -> int {
	Inner i = Inner(2, "deep");
	Outer o = Outer(7, "seven", i);

	// 1. Dotted path on a local: int field and string field.
	println("{}", "id={o.id}");
	println("{}", "name={o.name}");

	// 2. Nested dotted path — two levels of field access.
	println("{}", "depth={o.inner.depth}");
	println("{}", "label={o.inner.label}");

	// 3. Dotted path rooted at `self` inside a method body.
	println("{}", o.describe());

	// 4. `self` as a whole print argument, dispatched through Printable.
	o.show_self();

	// 5. A bare STRUCT placeholder inside an interpolated literal must render
	//    the same text as the `{}` format placeholder does.
	Plain p = Plain(41);
	println("{}", "interp=[{p}]");
	println("placeholder=[{}]", p);
	// The whole literal is ONE placeholder: the interpolation returns the
	// to_string result directly rather than concatenating, a distinct ownership
	// path from the multi-part case above.
	println("{}", "{p}");

	// 6. Several placeholders and literal text in one string.
	println("{}", "{o.id}/{o.name}/{o.inner.depth}");

	// 7. A format placeholder is NOT an identifier path and must be left alone
	//    for the format-string layer.
	println("fmt={} and {:x}", 12, 255);

	println("interp expressions codegen test passed!");
	return 0;
}
