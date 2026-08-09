import lib;

fn main() -> int {
	Todo t = Todo(1, "buy milk");
	// `id` is D15 metadata — queryable via `query Todo |> where { .id == ... }`,
	// but NOT nameable as an ordinary field access from another module.
	return t.id;
}
