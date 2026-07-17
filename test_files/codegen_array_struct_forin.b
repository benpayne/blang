// for-in over an Array of structs binds the loop variable to the element struct
// type, so field access on it resolves correctly (regression test).

struct Point {
	int x;
	int y;
}

fn main() -> int {
	Array<Point> pts = [ Point{x: 1, y: 2}, Point{x: 3, y: 4}, Point{x: 5, y: 6} ];

	int sumx = 0;
	int sumy = 0;
	for p in pts {
		sumx = sumx + p.x;
		sumy = sumy + p.y;
	}

	if sumx != 9 { return 1; }
	if sumy != 12 { return 2; }
	return 0;
}
