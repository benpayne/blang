// U6: recursion + mutual computation — identical result at -O0 and -O2. golden.
fn fib(int n) -> int {
    if n < 2 { return n; }
    return fib(n - 1) + fib(n - 2);
}
fn ackermann_ish(int m, int n) -> int {
    if m == 0 { return n + 1; }
    if n == 0 { return ackermann_ish(m - 1, 1); }
    return ackermann_ish(m - 1, ackermann_ish(m, n - 1));
}
fn main() -> int {
    println("fib(20) = {}", fib(20));
    println("ack(2,3) = {}", ackermann_ish(2, 3));
    return 0;
}
