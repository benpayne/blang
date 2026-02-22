// Test the ? try operator for error propagation

enum Result<T, E> {
    ok(T),
    err(E)
}

fn might_fail() -> Result<int, string> {
    return 42;
}

fn caller() -> Result<int, string> {
    var x = might_fail()?;
    return x;
}
