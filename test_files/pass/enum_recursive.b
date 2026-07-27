// Recursive and mutually-referencing enum payloads parse (boxed at codegen).
enum Tree {
	leaf(int),
	node(Tree, Tree)
}

fn depth(Tree t) -> int {
	match t {
		leaf(v) { return 1; }
		node(l, r) { return 1 + depth(l) + depth(r); }
	}
	return 0;
}

fn main() -> int {
	return depth(Tree.node(Tree.leaf(1), Tree.leaf(2)));
}
