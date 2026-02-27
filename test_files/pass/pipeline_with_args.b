fn add(int x, int y) -> int {
	return x + y;
}

fn multiply(int x, int factor) -> int {
	return x * factor;
}

fn main() -> int {
	int result = 5 |> add(10) |> multiply(3);
	return result;
}
