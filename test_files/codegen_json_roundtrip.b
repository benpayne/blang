extern fn puts(cstring s) -> int;

@json
struct Point {
	int x;
	int y;
}

fn main() -> int {
	Point p1 = Point { x: 0, y: 0 };
	string s1 = Point_to_json(p1);
	puts(s1);

	Point p2 = Point_from_json(s1);
	if p2.x != 0 { return 1; }
	if p2.y != 0 { return 2; }

	Point p3 = Point { x: -100, y: 2147483647 };
	string s3 = Point_to_json(p3);
	puts(s3);

	Point p4 = Point_from_json(s3);
	if p4.x != -100 { return 3; }
	if p4.y != 2147483647 { return 4; }

	string raw = "{\"x\":5,\"y\":10}";
	Point p5 = Point_from_json(raw);
	if p5.x != 5 { return 5; }
	if p5.y != 10 { return 6; }

	puts("JSON roundtrip test passed!");
	return 0;
}
