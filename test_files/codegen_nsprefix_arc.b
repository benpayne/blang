// U2 (modules-v2-graph): module-prefix string-ARC regression lock.
//
// Drives the NAMESPACED, module-PREFIXED `nsarc` module (see test_files/nsarc.b)
// through its internal string-returning call path — the config the retired
// qcc.cpp:303-307 rationale claimed double-freed. Deterministic (argv built in
// program) -> golden. Run under --leak-check against the ASan-instrumented
// runtime, this proves the prefixed path is leak/double-free clean, and the
// harness asserts `@nsarc__` internal callees are actually emitted.
import nsarc;

fn main() -> int {
    Array<string> argv = ["prog", "--name=alice", "--verbose", "-x", "--count=3"];

    println("name = {}", nsarc.value_of(argv, "name", "?"));
    println("has verbose = {}", nsarc.lookup(argv, "verbose"));
    println("count = {}", nsarc.value_of(argv, "count", "0"));
    println("missing = {}", nsarc.value_of(argv, "missing", "default"));
    println("has missing = {}", nsarc.lookup(argv, "missing"));

    // Stress the owned-string-return-across-prefix path in a loop so a refcount
    // imbalance accumulates into a detectable leak / double-free.
    int i = 0;
    int hits = 0;
    for i in 0..25 {
        string v = nsarc.value_of(argv, "name", "d");
        if v == "alice" { hits = hits + 1; }
    }
    println("hits = {}", hits);
    return 0;
}
