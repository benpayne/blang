// Array-typed struct field REASSIGNMENT (surfaced by the chat example's
// Room.leave): the field must take a counted reference from an existing-owner
// source (a scope-released local — previously the field dangled the moment
// the local died), and the previously-held array must be released (previously
// it leaked on every reassign). Covers: variable source inside a method,
// fresh-literal source, variable source at top level (reference semantics),
// and repeated churn.
struct Holder {
	Array<int> items;
}

impl Holder {
	fn drop_twos(self) {
		Array<int> keep = [];
		for v in self.items {
			if v != 2 {
				keep.push(v);
			}
		}
		self.items = keep;
	}
}

fn main() -> int {
	Holder h = Holder { items: [1, 2, 3] };
	h.drop_twos();
	println("{} {} {}", h.items.length, h.items[0], h.items[1]);

	// fresh literal reassign: previous array released
	h.items = [9, 8];
	println("{} {}", h.items.length, h.items[0]);

	// variable source: field and local share one counted array
	Array<int> ext = [4, 5, 6];
	h.items = ext;
	ext.push(7);
	println("{} {}", h.items.length, h.items[3]);

	// churn: repeated method reassignments stay balanced
	int rounds = 0;
	for i in 0..10 {
		h.drop_twos();
		rounds = rounds + 1;
	}
	println("{} {}", rounds, h.items.length);
	return 0;
}
