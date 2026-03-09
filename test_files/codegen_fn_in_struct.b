struct Handler {
    fn(int) -> int callback;
    string name;
}

fn double_it(int x) -> int {
    return x * 2;
}

fn main() -> int {
    Handler h = Handler { callback: double_it, name: "doubler" };

    // Call through struct field
    int result = h.callback(21);
    if result != 42 { return 1; }

    // Test with lambda
    Handler h2 = Handler { callback: fn(int x) -> int { return x + 10; }, name: "adder" };
    int result2 = h2.callback(5);
    if result2 != 15 { return 2; }

    println("Fn-in-struct test passed!");
    return 0;
}
