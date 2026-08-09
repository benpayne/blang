import todolib;

fn main() -> int {
	insert Todo { title: "buy milk", done: false };
	insert Todo { title: "walk dog", done: true };

	// query .field on an IMPORTED table struct (D15 forward direction).
	Array<Todo> all = query Todo |> order_by { .id };
	for t in all {
		// to_json on an IMPORTED @json struct (D15).
		println("{}", to_json(t));
	}

	Option<Todo> firstDone = query Todo |> where { .done == true } |> first;
	match firstDone {
		some(t) { println("first done: {}", to_json(t)); }
		none { println("none done"); }
	}
	return 0;
}
