// A @json table struct keeps its field declarations in the .bmod as D15
// compiler-facing metadata (so the struct stays queryable and serializable from
// a consumer). But those fields are STILL un-nameable in ordinary source — D15
// visibility is a resolution rule, not an emission rule.
@json
pub table struct Todo {
	int id;
	string title;
	bool done;
}

impl Todo {
	pub init(int i, string t) {
		self.id = i;
		self.title = t;
		self.done = false;
	}
}
