// Generic ARC: sort<T> over refcounted elements (T = string), called WITHOUT
// explicit type arguments (inference binds T from Array<string>). Exercises
// the monomorphized swap pattern `T tmp = items[j]` — the T-typed local is
// tracked, retained on bind, and released like a declared string — plus
// index-assignment element retain/release inside a generic body.
// (Previously: known-issue #4 — sort<string> was unsafe / crashed.)

fn sort_items<T>(Array<T> items) {
	for i in 0..items.length {
		for j in 0..items.length - 1 {
			if items[j + 1] < items[j] {
				T tmp = items[j];
				items[j] = items[j + 1];
				items[j + 1] = tmp;
			}
		}
	}
}

fn main() -> int {
	Array<string> names = ["delta", "alpha", "charlie", "bravo"];
	sort_items(names);
	for n in names {
		println("{}", n);
	}

	// Value types keep working through the same generic.
	Array<int> nums = [4, 1, 3, 2];
	sort_items(nums);
	for x in nums {
		println("{}", x);
	}
	return 0;
}
