// Regression: iterating/indexing an array of heap structs and binding an
// element to a local must retain the borrowed element. __blang_array_get
// returns a borrowed reference; without a retain on bind, the local's
// scope-exit release double-freed the element the array still owned.

struct Point {
	int x;
	int y;
}

fn main() -> int {
	Array<Point> pts = [];
	pts.push(Point { x: 1, y: 10 });
	pts.push(Point { x: 2, y: 20 });
	pts.push(Point { x: 3, y: 30 });

	int sum = 0;
	int i = 0;
	for i in 0..pts.length {
		Point p = pts[i];
		sum = sum + p.x + p.y;
	}

	// (1+10) + (2+20) + (3+30) = 66
	return sum - 66;
}
