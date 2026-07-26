// Interaction matrix (functional-hardening U3 / REQ-003): method-chain -> field
// in value contexts. Covers non-generic method-return->field, function-return->
// field, and the GENERIC method-chain->field (B1 fix): a generic method whose
// return type is a type parameter (V) must resolve to the concrete struct so
// `m.get(k).field` reads the right field (pre-fix it read empty). Printed AND
// asserted.

struct Point { int x; int y; }
struct Box { Point p; }
impl Box { fn get(self) -> Point { return self.p; } }

struct Map<K, V> { Array<K> keys; Array<V> values; }
impl Map {
	fn set(self, K k, V v) { self.keys.push(k); self.values.push(v); }
	fn get(self, K k) -> V {
		for i in 0..self.keys.length {
			if self.keys[i] == k { return self.values[i]; }
		}
		return self.values[0];
	}
}

fn make() -> Point { return Point { x: 5, y: 6 }; }

fn main() -> int {
	// non-generic method-return -> field
	Box b = Box { p: Point { x: 1, y: 2 } };
	println("chain_x {}", b.get().x);
	println("chain_y {}", b.get().y);
	assert b.get().x == 1, "box get x";
	assert b.get().y == 2, "box get y";

	// function-return -> field
	println("fnret {}", make().y);
	assert make().x == 5, "make x";

	// GENERIC method-chain -> field (B1). m.get("b").x must read 30, not empty.
	Map<string, Point> m = Map<string, Point> { keys: [], values: [] };
	m.set("a", Point { x: 10, y: 11 });
	m.set("b", Point { x: 30, y: 31 });
	println("gen_ax {}", m.get("a").x);
	println("gen_bx {}", m.get("b").x);
	println("gen_by {}", m.get("b").y);
	assert m.get("a").x == 10, "generic get a.x";
	assert m.get("b").x == 30, "generic get b.x";
	assert m.get("b").y == 31, "generic get b.y";

	println("PASS");
	return 0;
}
