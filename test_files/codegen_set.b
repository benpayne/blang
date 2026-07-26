// U5: hashed Set<K> — add/has/remove/dedup. Deterministic -> golden.
import collections;

fn main() -> int {
    Set<string> s = Set<string> { items: [], buckets: [] };

    s.add("apple");
    s.add("banana");
    s.add("cherry");
    s.add("apple");   // duplicate — no-op

    println("length = {}", s.length());
    println("has apple = {}", s.has("apple"));
    println("has banana = {}", s.has("banana"));
    println("has durian = {}", s.has("durian"));

    // Large-N to force growth/rehash, all retrievable.
    int i = 0;
    while i < 300 {
        s.add("item_{i}");
        i = i + 1;
    }
    println("length after 300 adds = {}", s.length());
    int all = 1;
    int j = 0;
    while j < 300 {
        if s.has("item_{j}") == false {
            all = 0;
        }
        j = j + 1;
    }
    println("all 300 present = {}", all);

    println("remove banana = {}", s.remove("banana"));
    println("has banana after remove = {}", s.has("banana"));
    println("remove missing = {}", s.remove("not_here"));
    return 0;
}
