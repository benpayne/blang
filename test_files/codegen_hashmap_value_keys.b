// Hashed Map/Set with non-string (value-type) keys: the compiler-reserved
// __blang_hash primitive dispatches on the monomorphized key type
// (string -> __blang_hash_string, int/long/double -> __blang_hash_bits).
import collections;

fn main() -> int {
    Map<int, string> m = Map<int, string>();
    for i in 0..50 {
        m.set(i, "v{i}");
    }
    println("int map: len={} get(7)={} has(49)={} has(99)={}", m.length(), m.get(7), m.has(49), m.has(99));
    m.set(7, "seven");
    println("overwrite: len={} get(7)={}", m.length(), m.get(7));

    Set<long> s = Set<long>();
    s.add(5);
    s.add(5);
    s.add(6);
    println("long set: len={} has(6)={} has(7)={}", s.length(), s.has(6), s.has(7));

    // KG-4 repro: Set<int> previously called __blang_hash_string(i32) -> ICE.
    Set<int> si = Set<int>();
    for i in 0..20 {
        si.add(i % 7);
    }
    println("int set: len={} has(3)={} has(9)={}", si.length(), si.has(3), si.has(9));

    Map<double, int> d = Map<double, int>();
    d.set(1.5, 15);
    d.set(2.25, 225);
    println("double map: get(2.25)={} has(3.0)={}", d.get(2.25), d.has(3.0));

    Map<string, float> f = Map<string, float>();
    f.set("a", 1.5);
    println("float value: {:.2f}", f.get("a"));
    return 0;
}
