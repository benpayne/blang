// U5 D15 fixture: an imported @json table struct. Its field SHAPE is its data
// contract (DB columns + JSON keys), so the .bmod keeps its field declarations
// as compiler-facing metadata — a consumer can `query`/`to_json` it, but cannot
// NAME a field in ordinary source (see fail/xmodule/imported_datacontract_field).
@json
pub table struct Todo {
	int id;
	string title;
	bool done;
}

impl Todo {
	pub init(int i, string t, bool d) {
		self.id = i;
		self.title = t;
		self.done = d;
	}
}
