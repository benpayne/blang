fn use_byte(byte b) -> int {
	int v = b;
	return v;
}

fn main() -> int {
	byte x = 42;
	return use_byte(x);
}
