// U4: env module via bcc import. Deterministic: PATH is present in the test
// run environment (asserted true), and a guaranteed-unset var yields none.
import env;

fn main() -> int {
    bool has_path = env.has("PATH");
    println("has PATH = {}", has_path);

    match env.get("__BLANG_DEFINITELY_UNSET_XYZ__") {
        some(v) { println("unexpected some: {}", v); }
        none { println("unset var = none"); }
    }

    string val = env.get_or("__BLANG_DEFINITELY_UNSET_XYZ__", "default_val");
    println("get_or fallback = {}", val);

    bool has_missing = env.has("__BLANG_DEFINITELY_UNSET_XYZ__");
    println("has missing = {}", has_missing);
    return 0;
}
