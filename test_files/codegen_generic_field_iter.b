// Generic ARC: iterating a generic struct's field at the CALLER. The field is
// declared with the struct's generic params (Box<T>'s `Array<T> items`), so the
// loop element type must resolve through the INSTANCE's type arguments —
// previously Sema annotated the field `Array<T>` (treating it as concrete),
// codegen skipped the instance substitution, and the loop variable was typed
// i32 while string ARC calls were emitted against it (IR verification failure).

struct Box<T> {
	Array<T> items;
}

fn main() -> int {
	Box<string> words = Box<string> { items: ["gamma", "alpha", "beta"] };
	for w in words.items {
		println("{}", w);
	}

	Box<int> nums = Box<int> { items: [10, 20, 30] };
	int total = 0;
	for n in nums.items {
		total = total + n;
	}
	println("{}", total);
	return 0;
}
