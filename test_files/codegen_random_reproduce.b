// U6: random fixed-seed reproducibility (two seedings -> identical stream). golden.
import random;
fn main() -> int {
    random.seed(777);
    long a1 = random.next();
    long a2 = random.next();
    long a3 = random.next();
    random.seed(777);
    long b1 = random.next();
    long b2 = random.next();
    long b3 = random.next();
    int same = 1;
    if a1 != b1 { same = 0; }
    if a2 != b2 { same = 0; }
    if a3 != b3 { same = 0; }
    println("seed 777 reproducible = {}", same);
    println("first = {}", a1);
    return 0;
}
