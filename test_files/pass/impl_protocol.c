protocol Printable {
	fn to_string() -> string;
}

struct Point {
	int x;
	int y;
}

impl Printable for Point {
	fn to_string() -> string {
		return "point";
	}
}

int main() {
	return 0;
}
