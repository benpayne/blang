// U6: an own value passed to an own parameter is moved; using it again is a
// use-after-move error (indirect move via a call).
fn consume(own string s) -> int {
	return 0;
}

fn main() -> int {
	own string a = "hello";
	int x = consume(a);
	int y = consume(a);
	return 0;
}
