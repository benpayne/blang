// Debug-info behavioral fixture (U3). Deterministic output (golden-checked in
// the normal suite); also built under -g in the DEBUG_INFO=1 suite leg and
// inspected by test_files/debug/dwarf_gdb_smoke.sh (subprogram-per-function
// count, line table naming this .b, a gdb breakpoint hit, and -g -O2 verify).
// Uses only plain functions so the DISubprogram count maps 1:1 to source
// functions (Vera architect finding #1).

fn add(int a, int b) -> int {
    return a + b;
}

fn factorial(int n) -> int {
    int result = 1;
    int i = 2;
    while i <= n {
        result = result * i;
        i = i + 1;
    }
    return result;
}

fn describe(int x) -> int {
    if x > 100 {
        return 1;
    }
    return 0;
}

fn main() -> int {
    int s = add(20, 22);
    int f = factorial(5);
    int d = describe(f);
    println("add = {}", s);
    println("factorial = {}", f);
    println("describe = {}", d);
    return 0;
}
