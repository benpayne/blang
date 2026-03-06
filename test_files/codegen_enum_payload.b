extern fn puts(string s) -> int;
extern fn printf(string fmt, ...) -> int;

enum Option {
    some(int),
    none
}

fn maybe_get(int x) -> Option {
    if x > 0 {
        return Option.some(x);
    }
    return Option.none;
}

fn main() -> int {
    Option a = maybe_get(42);
    match a {
        some(val) {
            if val != 42 { return 1; }
        }
        none {
            return 2;
        }
    }

    Option b = maybe_get(-1);
    match b {
        some(val) {
            return 3;
        }
        none {
            puts("None case works!");
        }
    }

    puts("Enum payload test passed!");
    return 0;
}
