// Protocol with non-fn declaration should fail

protocol Printable {
	int x;
}

fn main() -> int {
	return 0;
}
