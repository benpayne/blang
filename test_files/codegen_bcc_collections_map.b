// Stdlib-via-bcc (functional-hardening U4 / REQ-004 + seeded S2): Map from the
// `collections` MODULE, used through the real bcc driver. This is the S2
// regression: pre-fix, `Map<K,V> m` in a variable declaration failed to parse
// ("Failed parse varible") because a generic type from a namespaced combined
// stdlib module was invisible unqualified. Post-fix it resolves, monomorphizes,
// and runs. Also covers a Map<string, struct> value (a refcounted value type
// flowing through the module Map). Every value printed (golden) AND asserted.

import collections;

struct Point { int x; int y; }

fn main() -> int {
	Map<string, int> ages = Map<string, int> { keys: [], values: [] };
	ages.set("alice", 30);
	ages.set("bob", 25);
	println("alice={}", ages.get("alice"));
	println("bob={}", ages.get("bob"));
	println("has-alice={}", ages.has("alice"));
	println("has-carol={}", ages.has("carol"));
	println("len={}", ages.length());
	assert ages.get("alice") == 30, "get alice";
	assert ages.get("bob") == 25, "get bob";
	assert ages.has("alice") == true, "has alice";
	assert ages.has("carol") == false, "has carol";
	assert ages.length() == 2, "length";

	// Overwrite an existing key (set updates in place).
	ages.set("alice", 31);
	println("alice2={}", ages.get("alice"));
	assert ages.get("alice") == 31, "overwrite alice";
	assert ages.length() == 2, "length after overwrite";

	// Remove a key.
	ages.remove("bob");
	println("after-remove len={}", ages.length());
	println("has-bob={}", ages.has("bob"));
	assert ages.length() == 1, "length after remove";
	assert ages.has("bob") == false, "bob removed";

	// Map<string, struct>: a refcounted value type through the module Map.
	Map<string, Point> pts = Map<string, Point> { keys: [], values: [] };
	pts.set("origin", Point { x: 3, y: 4 });
	Point p = pts.get("origin");
	println("origin={},{}", p.x, p.y);
	assert p.x == 3, "point x";
	assert p.y == 4, "point y";

	println("PASS");
	return 0;
}
