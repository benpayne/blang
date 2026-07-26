// U4: time module via bcc import. Wall-clock values are non-deterministic, so
// this asserts INVARIANTS and prints a fixed `ok` line (golden-stable) — never a
// raw timestamp.
import time;

fn main() -> int {
    long t = time.now();
    long ms = time.now_millis();
    long m1 = time.monotonic_nanos();
    long m2 = time.monotonic_nanos();

    int ok = 1;
    // now() is well past 2001-09-09 (epoch 1_000_000_000) on any real clock.
    if t < 1000000000 {
        ok = 0;
    }
    // millis and seconds agree within a small window.
    if ms / 1000 < t - 2 {
        ok = 0;
    }
    // monotonic clock never goes backwards.
    if m2 < m1 {
        ok = 0;
    }
    println("time invariants ok = {}", ok);
    return 0;
}
