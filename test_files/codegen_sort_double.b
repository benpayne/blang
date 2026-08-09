// U6: generic sort<double> value-type sorting. golden.
import collections;
fn ltd(double a, double b) -> bool { return a < b; }
fn main() -> int {
    Array<double> a = [3.14, 1.41, 2.72, 0.58, 1.62];
    Array<double> s = collections.sort(a, ltd);
    print("sorted:");
    for x in s { print(" {:.2f}", x); }
    println("");
    return 0;
}
