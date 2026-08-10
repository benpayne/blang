// U5: hashed Map demonstration (done-condition #5 — "the Map is hashed, not the
// O(n) scan"). Inserts a large N of distinct string keys, proving the bucket
// table grows (power-of-two, > key count) AND that every key probes back to its
// own distinct value (a probe/rehash bug fails the readback). get_or on an
// absent key returns the fallback. Deterministic -> golden.
import collections;

fn key_for(int i) -> string {
    return "key_{i}";
}

fn main() -> int {
    Map<string, int> m = Map<string, int>();

    int N = 500;
    // Insert N distinct keys, each mapped to i*7.
    int i = 0;
    while i < N {
        m.set(key_for(i), i * 7);
        i = i + 1;
    }

    println("length = {}", m.length());

    // Structural evidence of hashing: the bucket table grew (doubling+rehash),
    // is a power of two, and is larger than the key count — a real open-address
    // table, not a vestigial array beside an O(n) scan. Read through the public
    // bucket_count() accessor (U6b-3/DC9: `m.buckets` is a private-field reach-in).
    int bl = m.bucket_count();
    println("buckets >= 512 = {}", bl >= 512);
    // power-of-two check: bl & (bl-1) == 0
    int pot = 0;
    if bl > 0 {
        // compute bl AND (bl-1) via subtraction-free check using modulo halving
        int x = bl;
        int is_pot = 1;
        while x > 1 {
            if x % 2 != 0 {
                is_pot = 0;
                x = 1;
            } else {
                x = x / 2;
            }
        }
        pot = is_pot;
    }
    println("buckets power-of-two = {}", pot);
    println("buckets > keys = {}", bl > m.length());

    // Probe evidence: every one of the N keys reads back its own distinct value.
    int all_ok = 1;
    int j = 0;
    while j < N {
        if m.get(key_for(j)) != j * 7 {
            all_ok = 0;
        }
        j = j + 1;
    }
    println("all {} keys read back correctly = {}", N, all_ok);

    // Overwrite an existing key updates in place (no duplicate).
    m.set(key_for(3), 999);
    println("overwrite key_3 = {}", m.get(key_for(3)));
    println("length after overwrite = {}", m.length());

    // get_or on an absent key returns the fallback (missing-return bug gone).
    println("absent get_or = {}", m.get_or("no_such_key", -1));
    println("has absent = {}", m.has("no_such_key"));

    // remove works and shrinks length.
    m.remove(key_for(0));
    println("has key_0 after remove = {}", m.has(key_for(0)));
    println("length after remove = {}", m.length());
    return 0;
}
