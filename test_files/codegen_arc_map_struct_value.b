// ARC matrix (functional-hardening U1 / REQ-001): a Map whose VALUES are
// refcounted heap structs. Map is defined INLINE (copied, as codegen_map.b does)
// so this test does not depend on U4's seeded Map-via-module (S2) fix. set/get
// store and fetch struct values; a fetched struct's field is read (twice) and
// the map is dropped (values released once each). Runs under --leak-check.
struct Map<K, V> {
	Array<K> keys;
	Array<V> values;
}

impl Map {
	fn length(self) -> int {
		return self.keys.length;
	}

	fn has(self, K key) -> bool {
		for i in 0..self.keys.length {
			if self.keys[i] == key {
				return true;
			}
		}
		return false;
	}

	fn set(self, K key, V value) {
		for i in 0..self.keys.length {
			if self.keys[i] == key {
				self.values[i] = value;
				return;
			}
		}
		self.keys.push(key);
		self.values.push(value);
	}

	fn get(self, K key) -> V {
		for i in 0..self.keys.length {
			if self.keys[i] == key {
				return self.values[i];
			}
		}
		return self.values[0];
	}
}

struct Item { int qty; string label; }

fn main() -> int {
	Map<string, Item> m = Map<string, Item> { keys: [], values: [] };
	m.set("apple", Item { qty: 3, label: "red" });
	m.set("pear", Item { qty: 5, label: "green" });
	println("len {}", m.length());
	assert m.has("apple") == true, "has apple";
	assert m.has("plum") == false, "no plum";

	Item a = m.get("apple");
	println("apple {} {}", a.qty, a.label);
	Item p = m.get("pear");
	println("pear {} {}", p.qty, p.label);
	// Fetch the same value a second time into a fresh binding and read it —
	// guards a stale read of a map-held struct value after other allocations.
	// (A direct field read off the generic call result — m.get(k).qty — is the
	// method-chain->field interaction shape owned by U3, not exercised here.)
	Item a2 = m.get("apple");
	println("apple-again {} {}", a2.qty, a2.label);
	assert a.qty == 3, "apple qty";
	assert p.label == "green", "pear label";
	assert a2.label == "red", "apple refetch label";

	println("PASS");
	return 0;
}
