// Generic ARC: refcounted values flowing through the hashed Map.
// - Map<string,string>: set/get/overwrite, values released exactly once
//   (previously leaked — the method-return temp was untracked at the caller).
// - Overwrite churn with interpolated keys (previously known-issue #3:
//   struct-valued Map intermittently heap-corrupted under churn).
// - Map<string, Array<int>>: an Array value survives set/get and scope exit
//   (previously known-issue #6: SEGV in the Map destructor).
import collections;

struct Point {
	int x;
	string label;
}

fn main() -> int {
	// String values: set, get, overwrite.
	Map<string, string> m = Map<string, string>();
	m.set("greeting", "hello");
	m.set("name", "ada");
	println("{}", m.get("greeting"));
	m.set("greeting", "hey");
	println("{}", m.get("greeting"));
	println("{}", m.get("name"));

	// Struct values under churn: interpolated keys, every entry overwritten.
	Map<string, Point> pts = Map<string, Point>();
	for i in 0..40 {
		pts.set("p{i}", Point { x: i, label: "point {i}" });
	}
	for i in 0..40 {
		pts.set("p{i}", Point { x: i + 100, label: "updated {i}" });
	}
	Point p = pts.get("p7");
	println("{} {}", p.x, p.label);
	println("{}", pts.length());

	// Array values: the stored array survives the Map's ownership round-trip.
	Map<string, Array<int>> groups = Map<string, Array<int>> { keys: [], values: [], buckets: [] };
	Array<int> evens = [2, 4, 6];
	groups.set("evens", evens);
	Array<int> got = groups.get("evens");
	println("{} {}", got.length, got[0]);
	return 0;
}
