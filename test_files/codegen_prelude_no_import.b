// modules-v2-graph U3 (D13): the prelude types Map, Set, and Buffer are in scope
// with ZERO import lines. This is the load-side D13 proof — a program using all
// three prelude types compiles and runs without importing anything.
// (Set<string> — Set<int> hits a pre-existing latent hashing bug unrelated to U3;
// see Known Issues.)
fn main() -> int {
    Map<string, int> m = Map<string, int>();
    m.set("x", 10);
    m.set("y", 20);
    println("map={}", m.length());

    Set<string> s = Set<string>();
    s.add("a");
    s.add("b");
    s.add("b");
    println("set={}", s.length());
    println("has_b={}", s.has("b"));

    Buffer b = Buffer(8);
    b.append_byte(65);
    b.append_byte(66);
    println("buflen={}", b.get_length());
    return 0;
}
