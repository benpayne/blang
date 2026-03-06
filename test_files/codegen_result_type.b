extern fn puts(string s) -> int;

enum Result {
    ok(int),
    err(string)
}

fn divide(int a, int b) -> Result {
    if b == 0 {
        return Result.err("division by zero");
    }
    return Result.ok(a / b);
}

fn main() -> int {
    Result r1 = divide(10, 2);
    match r1 {
        ok(val) {
            if val != 5 { return 1; }
        }
        err(msg) {
            return 2;
        }
    }

    Result r2 = divide(10, 0);
    match r2 {
        ok(val) {
            return 3;
        }
        err(msg) {
            puts(msg);
        }
    }

    puts("Result type test passed!");
    return 0;
}
