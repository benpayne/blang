// A declared-but-never-used local variable emits a `warning:`.
// Without -Werror the compile still succeeds (exit 0); with -Werror it fails.
fn main() -> int {
    int unused = 42;
    println("hello");
    return 0;
}
