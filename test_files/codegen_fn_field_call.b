struct Callback {
    fn(int) -> int handler;
}

fn triple(int x) -> int {
    return x * 3;
}

struct Router {
    fn(int, int) -> int compute;
    string label;
}

fn add(int a, int b) -> int {
    return a + b;
}

fn main() -> int {
    // Test 1: simple fn-typed field call
    Callback cb = Callback { handler: triple };
    int result = cb.handler(10);
    if result != 30 { return 1; }

    // Test 2: fn-typed field with lambda
    Callback cb2 = Callback { handler: fn(int x) -> int { return x * x; } };
    int result2 = cb2.handler(7);
    if result2 != 49 { return 2; }

    // Test 3: struct with multiple fields including fn type
    Router r = Router { compute: add, label: "adder" };
    int result3 = r.compute(10, 20);
    if result3 != 30 { return 3; }

    // Test 4: void-returning fn-typed field
    // (test that void fn fields work too)

    println("Field call test passed!");
    return 0;
}
