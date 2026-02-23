// Two functions with the same name should be rejected (no overloading in BLang)

fn add(int a, int b) -> int {
	return a;
}

fn add(int a, int b) -> int {
	return b;
}

fn main() -> int {
	return 0;
}
