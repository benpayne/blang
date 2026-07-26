// U6: hashed Set dedup + large-N growth + remove. golden.
import collections;
fn main() -> int {
    Set<string> s = Set<string> { items: [], buckets: [] };
    s.add("red");
    s.add("green");
    s.add("red");
    s.add("blue");
    s.add("green");
    println("length after dups = {}", s.length());
    int i = 0;
    while i < 250 {
        s.add("c{i}");
        i = i + 1;
    }
    println("length after 250 = {}", s.length());
    int all = 1;
    int j = 0;
    while j < 250 {
        if s.has("c{j}") == false { all = 0; }
        j = j + 1;
    }
    println("all 250 present = {}", all);
    println("has red = {}", s.has("red"));
    println("remove red = {}", s.remove("red"));
    println("has red after = {}", s.has("red"));
    return 0;
}
