// modules-v2-graph U8 (DC10): import aliasing. `import mathlib as m;` binds the
// dependency to the LOCAL qualifier `m`. The dependency's functions are reached
// qualified through the alias (`m.add`, `m.largest`), and D7 name-capability rides
// on the alias too — `Pair<int>` is namable because `mathlib` was imported (under
// `m`). The emitted symbols are the library's real ones (`add`, `largest_...`),
// linked from mathlib.a — qualification/aliasing is a pure source spelling.
import mathlib as m;

fn main() -> int {
	println("3 + 4 = {}", m.add(3, 4));
	println("5 * 6 = {}", m.multiply(5, 6));
	println("largest = {}", m.largest(9, 4));
	Pair<int> p = Pair<int>(10, 32);
	println("sum = {}", p.sum());
	return 0;
}
