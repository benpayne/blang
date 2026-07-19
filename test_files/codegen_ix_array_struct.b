// Interaction matrix (functional-hardening U3 / REQ-003): Array<struct> element
// field read and mutate, and for-in over an Array<struct> using element fields.
// Printed AND asserted.

struct Point { int x; int y; }

fn main() -> int {
	Array<Point> pts = [Point { x: 1, y: 10 }, Point { x: 2, y: 20 }, Point { x: 3, y: 30 }];

	// element field read
	println("read {}", pts[1].x);
	assert pts[1].x == 2, "elem read";

	// element field mutate
	pts[0].x = 42;
	pts[2].y = 99;
	println("mut0 {}", pts[0].x);
	println("mut2 {}", pts[2].y);
	assert pts[0].x == 42, "elem mutate 0";
	assert pts[2].y == 99, "elem mutate 2";
	// unmutated fields stay intact
	assert pts[0].y == 10, "elem 0 y intact";
	assert pts[2].x == 3, "elem 2 x intact";

	// for-in over Array<struct> summing an element field
	int sumx = 0;
	int sumy = 0;
	for p in pts {
		sumx += p.x;
		sumy += p.y;
	}
	println("sumx {}", sumx);   // 42 + 2 + 3 = 47
	println("sumy {}", sumy);   // 10 + 20 + 99 = 129
	assert sumx == 47, "for-in sum x";
	assert sumy == 129, "for-in sum y";

	println("PASS");
	return 0;
}
