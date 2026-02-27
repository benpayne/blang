fn add_one(int x) -> int {
	return x + 1;
}

fn twice(int x) -> int {
	return x * 2;
}

fn main() -> int {
	int result = 5 |> add_one |> twice;
	return result;
}
