// Reassigning a refcounted variable from a BORROWED source (another variable,
// an array element, a field) must retain: the destination is a second owner.
// Previously every case here was a heap-use-after-free at scope exit.
struct Q {
    int n;
}

struct P {
    Q q;
    Array<int> xs;
}

fn main() -> int {
    string a = "x{1}";
    string b = "y";
    b = a;
    a = a;
    println("string var: {} {}", a, b);

    Array<string> names = ["n0", "n1"];
    string pick = "none";
    pick = names[1];
    println("string elem: {}", pick);

    Array<int> xs = [1, 2, 3];
    Array<int> ys = [9];
    ys = xs;
    println("array var: {} {}", xs.length, ys.length);

    P p = P { q: Q { n: 3 }, xs: [4, 5] };
    Q q = Q { n: 2 };
    q = p.q;
    Array<int> zs = [0];
    zs = p.xs;
    println("struct field: {} array field: {}", q.n, zs.length);

    Q r = Q { n: 7 };
    Q s = Q { n: 8 };
    s = r;
    println("struct var: {} {}", r.n, s.n);

    own string o1 = "o{1}";
    own string o2 = "o2";
    o2 = o1;
    println("own move: {}", o2);
    return 0;
}
