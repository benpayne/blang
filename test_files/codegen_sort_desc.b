// U6: generic sort<int> descending + a larger array. golden.
import collections;
fn gt(int a, int b) -> bool { return a > b; }
fn lt(int a, int b) -> bool { return a < b; }
fn main() -> int {
    Array<int> a = [3, 1, 4, 1, 5, 9, 2, 6, 5, 3, 5];
    Array<int> d = sort<int>(a, gt);
    print("desc:");
    for x in d { print(" {}", x); }
    println("");
    Array<int> b = [3, 1, 4, 1, 5, 9, 2, 6, 5, 3, 5];
    Array<int> asc = sort<int>(b, lt);
    print("asc:");
    for x in asc { print(" {}", x); }
    println("");
    return 0;
}
