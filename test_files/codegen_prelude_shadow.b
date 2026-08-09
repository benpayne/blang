// modules-v2-graph U3 (D13, §5b): a module's own definition SHADOWS a prelude name.
// This user Map has a DISTINCT shape and API (put/size) — the prelude Map has
// set/length and hash buckets. With the prelude unconditionally loaded, this must
// still compile and run against the USER's Map (proving shadow, not clash): if the
// prelude Map leaked in, `m.put`/`m.size` would not resolve. Zero imports.
struct Map<K, V> { Array<K> keys; Array<V> vals; }

impl Map {
    pub init() { self.keys = []; self.vals = []; }
    fn put(self, K k, V v) { self.keys.push(k); self.vals.push(v); }
    fn size(self) -> int { return self.keys.length; }
}

fn main() -> int {
    Map<string, int> m = Map<string, int>();
    m.put("a", 1);
    m.put("b", 2);
    m.put("c", 3);
    println("mine={}", m.size());
    return 0;
}
