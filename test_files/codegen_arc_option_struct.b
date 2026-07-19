// ARC matrix (functional-hardening U1 / REQ-001): Option<struct> — a refcounted
// heap struct carried as an Option payload (a pointer, fits the 8-byte payload),
// unwrapped via match to a field on the some arm, with a none arm; both paths
// leak-clean. Runs under --leak-check.
struct Node { int val; string tag; }

fn make(bool present, int v) -> Option<Node> {
	if present {
		return Option.some(Node { val: v, tag: "n" });
	}
	return Option.none;
}

fn describe(Option<Node> o) -> int {
	match o {
		some(n) {
			println("some {} {}", n.val, n.tag);
			return n.val;
		}
		none {
			println("none");
			return 0;
		}
	}
}

fn main() -> int {
	Option<Node> a = make(true, 42);
	Option<Node> b = make(false, 0);

	int va = describe(a);
	int vb = describe(b);
	assert va == 42, "some payload field";
	assert vb == 0, "none path";

	// A second some, unwrapped inline.
	match make(true, 7) {
		some(n) {
			println("inline {}", n.val);
			assert n.val == 7, "inline some";
		}
		none {
			println("unexpected none");
		}
	}

	println("PASS");
	return 0;
}
