// Optimization-friendly arithmetic: inlinable helpers, common subexpressions,
// a loop with a reduction, and recursion. The result must be identical at every
// -O level (the OPT_LEVEL=2 suite run re-checks this golden under optimization).

fn square(int x) -> int {
    return x * x;
}

fn sum_of_squares(int n) -> int {
    int total = 0;
    int i = 1;
    while i <= n {
        total = total + square(i);
        i = i + 1;
    }
    return total;
}

fn fib(int n) -> int {
    if n < 2 {
        return n;
    }
    return fib(n - 1) + fib(n - 2);
}

fn main() -> int {
    int ss = sum_of_squares(10);        // 385
    int f = fib(15);                    // 610
    int combined = ss + f;              // 995
    println("sum_of_squares(10) = {}", ss);
    println("fib(15) = {}", f);
    println("combined = {}", combined);
    return 0;
}
