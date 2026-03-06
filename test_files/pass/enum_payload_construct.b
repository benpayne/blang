// Test parsing enum variant construction expressions

enum Option {
    some(int),
    none
}

enum Result {
    ok(int),
    err(string)
}

fn make_some(int x) -> Option {
    return Option.some(x);
}

fn make_none() -> Option {
    return Option.none;
}

fn make_ok(int x) -> Result {
    return Result.ok(x);
}

fn make_err(string msg) -> Result {
    return Result.err(msg);
}

fn main() -> int {
    Option a = Option.some(42);
    Option b = Option.none;
    Result r = Result.ok(10);
    return 0;
}
