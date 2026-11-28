// modules-v2-lsp U1 project_compile ctest, case C4: a user struct Map with a
// distinct API shadows the prelude Map; Set still comes from the prelude.
struct Map<K, V> { Array<K> keys; Array<V> vals; }

impl Map {
    pub init() { self.keys = []; self.vals = []; }
    fn put(self, K k, V v) { self.keys.push(k); self.vals.push(v); }
    fn size(self) -> int { return self.keys.length; }
}

fn main() -> int {
    Map<string, int> m = Map<string, int>();
    m.put("a", 1);
    Set<int> s = Set<int>();
    return m.size() + s.length();
}
