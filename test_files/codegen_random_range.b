// U6: random int_range bounds over many draws + float01 bounds. golden.
import random;
fn main() -> int {
    random.seed(2024);
    int in_bounds = 1;
    int i = 0;
    while i < 1000 {
        long v = random.int_range(50, 60);
        if v < 50 { in_bounds = 0; }
        if v >= 60 { in_bounds = 0; }
        i = i + 1;
    }
    println("int_range [50,60) 1000 draws in bounds = {}", in_bounds);
    int f_bounds = 1;
    int j = 0;
    while j < 1000 {
        double f = random.float01();
        if f < 0.0 { f_bounds = 0; }
        if f >= 1.0 { f_bounds = 0; }
        j = j + 1;
    }
    println("float01 1000 draws in [0,1) = {}", f_bounds);
    return 0;
}
