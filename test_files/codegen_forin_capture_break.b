// KI-24 regression: capturing a for-in loop ELEMENT into an outer refcounted
// variable and then leaving the loop early (break / return) must be
// ARC-balanced — the outer variable owns its own reference.
fn find_first(Array<string> a, string want) -> string {
    for s in a {
        if s == want {
            return s;
        }
    }
    return "none";
}

fn main() -> int {
    Array<string> a = ["x", "y", "z"];
    string found = "none";
    for s in a {
        if s == "y" {
            found = s;
            break;
        }
    }
    println("break: {}", found);

    string last = "none";
    for s in a {
        last = s;
        if s == "z" {
            continue;
        }
    }
    println("continue: {}", last);

    println("return: {}", find_first(a, "z"));

    Array<string> b = ["p", "q"];
    string kept = "none";
    for s in b {
        kept = s;
        break;
    }
    println("immediate break: {}", kept);
    return 0;
}
