// U6: env get_or defaults + has hit/miss (PATH present, unset var absent). golden.
import env;
fn main() -> int {
    println("has PATH = {}", env.has("PATH"));
    println("has __NOPE__ = {}", env.has("__BLANG_U6_UNSET__"));
    println("get_or unset = {}", env.get_or("__BLANG_U6_UNSET__", "fallback"));
    match env.get("__BLANG_U6_UNSET__") {
        some(v) { println("unexpected {}", v); }
        none { println("unset is none"); }
    }
    return 0;
}
