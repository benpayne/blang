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
    }

    fn remove(self, K key) -> bool {
        for i in 0..self.keys.length {
            if self.keys[i] == key {
                for j in i..self.keys.length - 1 {
                    self.keys[j] = self.keys[j + 1];
                    self.values[j] = self.values[j + 1];
                }
                self.keys.pop();
                self.values.pop();
                return true;
            }
        }
        return false;
    }
}

fn main() -> int {
    Map<string, int> ages = Map<string, int> { keys: [], values: [] };

    // Test empty map
    if ages.length() != 0 { return 1; }

    // Test set and has
    ages.set("alice", 30);
    if ages.has("alice") != true { return 2; }
    if ages.has("bob") != false { return 3; }

    // Test get
    if ages.get("alice") != 30 { return 4; }

    // Test multiple entries
    ages.set("bob", 25);
    ages.set("charlie", 35);
    if ages.length() != 3 { return 5; }
    if ages.get("bob") != 25 { return 6; }

    // Test update existing key
    ages.set("alice", 31);
    if ages.get("alice") != 31 { return 7; }
    if ages.length() != 3 { return 8; }

    // Test remove existing key
    if ages.remove("bob") != true { return 9; }
    if ages.length() != 2 { return 10; }
    if ages.has("bob") != false { return 11; }

    // Test remove non-existent key
    if ages.remove("dave") != false { return 12; }
    if ages.length() != 2 { return 13; }

    // Verify remaining entries after remove
    if ages.get("alice") != 31 { return 14; }
    if ages.get("charlie") != 35 { return 15; }

    println("Map test passed!");
    return 0;
}
