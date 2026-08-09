// U5: generic comparator-based sort<T> for value-type elements — int (ascending
// + descending, two comparators) and double (ascending). golden. (Sorting
// refcounted elements like strings awaits an array-element ARC fix; see the
// spec and known-issues.)
import collections;

fn less_int(int a, int b) -> bool { return a < b; }
fn greater_int(int a, int b) -> bool { return a > b; }
fn less_double(double a, double b) -> bool { return a < b; }

fn main() -> int {
    // Generic sort<int>, ascending then descending.
    Array<int> a = [5, 2, 8, 1, 9, 3, 7];
    Array<int> asc = collections.sort(a, less_int);
    print("int asc:");
    for x in asc { print(" {}", x); }
    println("");

    Array<int> b = [5, 2, 8, 1, 9, 3, 7];
    Array<int> desc = collections.sort(b, greater_int);
    print("int desc:");
    for x in desc { print(" {}", x); }
    println("");

    // Generic sort<double>, ascending (a second element type).
    Array<double> d = [3.5, 1.2, 2.8, 0.5];
    Array<double> dasc = collections.sort(d, less_double);
    print("double asc:");
    for x in dasc { print(" {:.1f}", x); }
    println("");

    // Single-element is a no-op.
    Array<int> one = [42];
    Array<int> one_s = collections.sort(one, less_int);
    println("single: {}", one_s[0]);
    return 0;
}
