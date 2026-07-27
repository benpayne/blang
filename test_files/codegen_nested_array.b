// End-to-end: nested generic containers — Array<Array<int>> built, indexed,
// iterated, and mutated. Exercises the ">>" close-bracket split plus the ARC
// rules the nesting depends on: push retains the inner array (the outer array
// owns a real reference released via its element destructor), and binding an
// element to a local (Array<int> row = grid[i]) retains the borrowed element.

fn main() -> int {
	Array<Array<int>> grid = [];

	Array<int> r0 = [1, 2, 3];
	Array<int> r1 = [10, 20];
	grid.push(r0);
	grid.push(r1);

	println("rows={}", grid.length);

	// Bind an element to a local and read through it.
	Array<int> first = grid[0];
	println("first row len={} first elem={}", first.length, first[0]);

	// Iterate rows, summing every element.
	int total = 0;
	for i in 0..grid.length {
		Array<int> row = grid[i];
		for j in 0..row.length {
			total = total + row[j];
		}
	}
	println("total={}", total);

	// Mutating through the original local is visible through the grid
	// (arrays are shared references, not copies).
	r1.push(30);
	Array<int> second = grid[1];
	println("second row len={} last={}", second.length, second[2]);

	if total != 36 {
		return 1;
	}
	return 0;
}
