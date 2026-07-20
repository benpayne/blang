// U6: time invariants (monotonic never decreases; millis/seconds agree). golden.
import time;
fn main() -> int {
    long a = time.monotonic_nanos();
    long b = time.monotonic_nanos();
    long c = time.monotonic_nanos();
    int nondecreasing = 1;
    if b < a { nondecreasing = 0; }
    if c < b { nondecreasing = 0; }
    println("monotonic nondecreasing = {}", nondecreasing);
    long s = time.now();
    long ms = time.now_millis();
    int agree = 1;
    if ms / 1000 < s - 2 { agree = 0; }
    if ms / 1000 > s + 2 { agree = 0; }
    println("millis agrees with seconds = {}", agree);
    return 0;
}
