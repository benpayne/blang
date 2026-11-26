struct P { int n; }
fn main() -> int {
    P p = P { n: 1 };
    float f = 2.5;
    p.n = f;
    return p.n;
}
