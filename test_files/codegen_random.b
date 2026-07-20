// U4: random module via bcc import. Deterministic — a fixed seed produces a
// fixed sequence (dedicated SplitMix64 C PRNG, platform-stable), so the exact
// stream is golden-checked.
import random;

fn main() -> int {
    random.seed(42);
    long a = random.next();
    long b = random.next();
    long c = random.next();
    println("seq: {} {} {}", a, b, c);

    // Re-seed with the same seed -> identical stream (reproducibility).
    random.seed(42);
    long a2 = random.next();
    println("reproduced first = {}", a2);

    // Bounded range: always within [10, 20).
    random.seed(7);
    int in_range = 1;
    int i = 0;
    while i < 100 {
        long v = random.int_range(10, 20);
        if v < 10 {
            in_range = 0;
        }
        if v >= 20 {
            in_range = 0;
        }
        i = i + 1;
    }
    println("int_range in [10,20) for 100 draws = {}", in_range);
    return 0;
}
