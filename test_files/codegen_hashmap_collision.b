// U6: hashed Map under many keys forcing probing/collisions, with updates and
// removes; all survivors read back correctly. golden.
import collections;
fn main() -> int {
    Map<string, int> m = Map<string, int>();
    int i = 0;
    while i < 200 {
        m.set("n{i}", i);
        i = i + 1;
    }
    println("length = {}", m.length());
    // update half in place
    int j = 0;
    while j < 200 {
        if j % 2 == 0 {
            m.set("n{j}", j * 100);
        }
        j = j + 1;
    }
    println("length after updates = {}", m.length());
    // verify
    int ok = 1;
    int k = 0;
    while k < 200 {
        int expect = k;
        if k % 2 == 0 { expect = k * 100; }
        if m.get("n{k}") != expect { ok = 0; }
        k = k + 1;
    }
    println("all 200 correct after update = {}", ok);
    // remove a chunk
    int r = 0;
    while r < 50 {
        m.remove("n{r}");
        r = r + 1;
    }
    println("length after 50 removes = {}", m.length());
    println("has n0 = {}", m.has("n0"));
    println("has n100 = {}", m.has("n100"));
    return 0;
}
