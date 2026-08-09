// stdlib/collections.b — Hashed Map<K,V> + Set<K> + generic sort.
//
// Usage: import collections;
//
// Map<K,V> is a HASHED map (open addressing, FNV-1a via __blang_hash_string),
// replacing the former O(n) parallel-array scan. Keys are strings (the only key
// type in use; the hash primitive is string-based). `buckets[slot]` holds
// (keyIndex + 1); 0 means empty. The table grows (doubling + rehash) when the
// load factor exceeds 0.7, so `has`/`get`/`set` are O(1) average.
//
// Construction: Map<string,int> { keys: [], values: [], buckets: [] }
// (buckets is lazily sized on the first set()).

extern fn __blang_hash_string(string s) -> int;

pub struct Map<K, V> {
    Array<K> keys;
    Array<V> values;
    Array<int> buckets;   // open-addressing index table: (keyIndex+1), 0 = empty
}

impl Map {
    fn length(self) -> int {
        return self.keys.length;
    }

    fn is_empty(self) -> bool {
        return self.keys.length == 0;
    }

    // Reset the bucket table to `new_cap` empty slots and re-insert every
    // existing key's index. Called on first use (lazy init) and on growth.
    fn rehash(self, int new_cap) {
        self.buckets.clear();
        for i in 0..new_cap {
            self.buckets.push(0);
        }
        for i in 0..self.keys.length {
            int h = __blang_hash_string(self.keys[i]);
            int slot = h % new_cap;
            while self.buckets[slot] != 0 {
                slot = slot + 1;
                if slot >= new_cap {
                    slot = 0;
                }
            }
            self.buckets[slot] = i + 1;
        }
    }

    fn ensure_buckets(self) {
        if self.buckets.length == 0 {
            self.rehash(16);
        }
    }

    // Return the slot where `key` lives, or the empty slot where it would be
    // inserted. Assumes buckets are initialized and load factor < 1 (there is
    // always an empty slot, so the probe terminates).
    fn find_slot(self, K key) -> int {
        int cap = self.buckets.length;
        int h = __blang_hash_string(key);
        int slot = h % cap;
        while self.buckets[slot] != 0 {
            int idx = self.buckets[slot] - 1;
            if self.keys[idx] == key {
                return slot;
            }
            slot = slot + 1;
            if slot >= cap {
                slot = 0;
            }
        }
        return slot;
    }

    fn has(self, K key) -> bool {
        if self.buckets.length == 0 {
            return false;
        }
        int slot = self.find_slot(key);
        return self.buckets[slot] != 0;
    }

    fn set(self, K key, V value) {
        self.ensure_buckets();
        int slot = self.find_slot(key);
        if self.buckets[slot] != 0 {
            int idx = self.buckets[slot] - 1;
            self.values[idx] = value;
            return;
        }
        self.keys.push(key);
        self.values.push(value);
        self.buckets[slot] = self.keys.length;   // (index + 1)
        int cap = self.buckets.length;
        // Grow when load factor > 0.7 (keys*10 > cap*7).
        if self.keys.length * 10 > cap * 7 {
            self.rehash(cap * 2);
        }
    }

    // get(key) requires the key be present. On an absent key it aborts with a
    // located message (a DEFINED failure, replacing the former missing-return
    // undefined behavior). Use has()/get_or() for a non-aborting path.
    fn get(self, K key) -> V {
        if self.buckets.length != 0 {
            int slot = self.find_slot(key);
            if self.buckets[slot] != 0 {
                int idx = self.buckets[slot] - 1;
                return self.values[idx];
            }
        }
        assert false, "Map.get: key not present (use has()/get_or())";
        return self.values[0];   // unreachable; satisfies the return checker
    }

    fn get_or(self, K key, V fallback) -> V {
        if self.buckets.length == 0 {
            return fallback;
        }
        int slot = self.find_slot(key);
        if self.buckets[slot] != 0 {
            int idx = self.buckets[slot] - 1;
            return self.values[idx];
        }
        return fallback;
    }

    fn remove(self, K key) -> bool {
        if self.buckets.length == 0 {
            return false;
        }
        int slot = self.find_slot(key);
        if self.buckets[slot] == 0 {
            return false;
        }
        int idx = self.buckets[slot] - 1;
        // Shift the key/value arrays down over the removed index, then rebuild
        // the bucket index (indices shifted). O(n) remove — correct and simple.
        for j in idx..self.keys.length - 1 {
            self.keys[j] = self.keys[j + 1];
            self.values[j] = self.values[j + 1];
        }
        self.keys.pop();
        self.values.pop();
        self.rehash(self.buckets.length);
        return true;
    }

    // Accessor surface (modules-v2-exports): the accessor takes the field's own
    // name, so a consumer migrates by adding `()`. `collections` is promoted into
    // the user's scope this epic (exempt from module-private enforcement, A7),
    // so these follow the module's unmarked-`fn` style.
    fn keys(self) -> Array<K> {
        return self.keys;
    }

    fn values(self) -> Array<V> {
        return self.values;
    }
}

// Set<K> — a hashed set of string keys (same open-addressing scheme as Map).
pub struct Set<K> {
    Array<K> items;
    Array<int> buckets;
}

impl Set {
    fn length(self) -> int {
        return self.items.length;
    }

    fn is_empty(self) -> bool {
        return self.items.length == 0;
    }

    fn rehash(self, int new_cap) {
        self.buckets.clear();
        for i in 0..new_cap {
            self.buckets.push(0);
        }
        for i in 0..self.items.length {
            int h = __blang_hash_string(self.items[i]);
            int slot = h % new_cap;
            while self.buckets[slot] != 0 {
                slot = slot + 1;
                if slot >= new_cap {
                    slot = 0;
                }
            }
            self.buckets[slot] = i + 1;
        }
    }

    fn find_slot(self, K key) -> int {
        int cap = self.buckets.length;
        int h = __blang_hash_string(key);
        int slot = h % cap;
        while self.buckets[slot] != 0 {
            int idx = self.buckets[slot] - 1;
            if self.items[idx] == key {
                return slot;
            }
            slot = slot + 1;
            if slot >= cap {
                slot = 0;
            }
        }
        return slot;
    }

    fn has(self, K key) -> bool {
        if self.buckets.length == 0 {
            return false;
        }
        int slot = self.find_slot(key);
        return self.buckets[slot] != 0;
    }

    fn add(self, K key) {
        if self.buckets.length == 0 {
            self.rehash(16);
        }
        int slot = self.find_slot(key);
        if self.buckets[slot] != 0 {
            return;   // already present
        }
        self.items.push(key);
        self.buckets[slot] = self.items.length;
        int cap = self.buckets.length;
        if self.items.length * 10 > cap * 7 {
            self.rehash(cap * 2);
        }
    }

    fn remove(self, K key) -> bool {
        if self.buckets.length == 0 {
            return false;
        }
        int slot = self.find_slot(key);
        if self.buckets[slot] == 0 {
            return false;
        }
        int idx = self.buckets[slot] - 1;
        for j in idx..self.items.length - 1 {
            self.items[j] = self.items[j + 1];
        }
        self.items.pop();
        self.rehash(self.buckets.length);
        return true;
    }

    // Accessor surface (modules-v2-exports): takes the field's own name.
    fn items(self) -> Array<K> {
        return self.items;
    }
}

// Generic comparator-based sort. In-place insertion sort (O(n^2) — a stdlib
// convenience, not a perf primitive) over `items`, ordering by `less(a,b)`
// (true when a should come before b). The sorted array is returned as well.
//
// Works for value-type elements (int, double, bool, char, ...) AND refcounted
// elements (string): the generic-ARC unit made monomorphized `T`-typed locals
// participate in refcounting, so the swap's `T tmp = items[j]` retains/releases
// correctly (locked in by codegen_generic_arc_sort.b and the wordfreq example).
// Comparators may be named functions or lambdas.
pub fn sort<T>(Array<T> items, fn(T, T) -> bool less) -> Array<T> {
    int n = items.length;
    for i in 1..n {
        int j = i;
        while j > 0 {
            if less(items[j], items[j - 1]) {
                T tmp = items[j];
                items[j] = items[j - 1];
                items[j - 1] = tmp;
                j = j - 1;
            } else {
                j = 0;
            }
        }
    }
    return items;
}
