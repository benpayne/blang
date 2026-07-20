// U6: loop-heavy arithmetic — identical golden at -O0 and -O2 (opt loop xforms). golden.
fn main() -> int {
    long sum = 0;
    int i = 0;
    while i < 10000 {
        sum = sum + i;
        i = i + 1;
    }
    println("sum 0..9999 = {}", sum);
    int product = 1;
    int j = 1;
    while j <= 10 {
        product = product * j;
        j = j + 1;
    }
    println("10! = {}", product);
    int count = 0;
    int k = 2;
    while k < 100 {
        int d = 2;
        int prime = 1;
        while d * d <= k {
            if k % d == 0 { prime = 0; }
            d = d + 1;
        }
        if prime == 1 { count = count + 1; }
        k = k + 1;
    }
    println("primes < 100 = {}", count);
    return 0;
}
