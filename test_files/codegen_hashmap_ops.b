// U6: hashed Map get_or/has/length/is_empty combinations. golden.
import collections;
fn main() -> int {
    Map<string, int> m = Map<string, int>();
    println("empty = {}", m.is_empty());
    m.set("a", 10);
    m.set("b", 20);
    m.set("a", 11);
    println("empty after set = {}", m.is_empty());
    println("length = {}", m.length());
    println("get a = {}", m.get("a"));
    println("get_or b = {}", m.get_or("b", -1));
    println("get_or missing = {}", m.get_or("zzz", -1));
    println("has a = {}", m.has("a"));
    println("has zzz = {}", m.has("zzz"));
    println("remove b = {}", m.remove("b"));
    println("remove missing = {}", m.remove("zzz"));
    println("final length = {}", m.length());
    return 0;
}
