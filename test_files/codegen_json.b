extern fn puts(cstring s) -> int;

@json
struct Point {
	int x;
	int y;
}

fn main() -> int {
	Point p = Point { x: 10, y: 20 };
	string s = Point_to_json(p);
	puts(s);

	Point p2 = Point_from_json(s);

	if p2.x != 10 { return 1; }
	if p2.y != 20 { return 1; }

	puts("JSON codegen test passed!");
	return 0;
}
