extern fn puts(cstring s) -> int;

enum Result {
    ok(int),
    err(int)
}

fn might_fail(int x) -> Result {
    if x < 0 {
        return Result.err(-1);
    }
    return Result.ok(x * 2);
}

fn use_result(int x) -> Result {
    int val = might_fail(x)?;
    return Result.ok(val + 1);
}

fn main() -> int {
    // Test success path: might_fail(5) -> ok(10), then val+1 -> ok(11)
    Result r1 = use_result(5);
    match r1 {
        ok(v) {
            if v != 11 { return 1; }
        }
        err(e) {
            return 2;
        }
    }

    // Test error path: might_fail(-1) -> err(-1), propagated through use_result
    Result r2 = use_result(-1);
    match r2 {
        ok(v) {
            return 3;
        }
        err(e) {
            if e != -1 { return 4; }
        }
    }

    puts("Try operator test passed!");
    return 0;
}
