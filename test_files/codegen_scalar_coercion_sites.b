// Scalar coercion at every store/call boundary: call args, method args,
// field stores, compound assignment (variables and fields), array push,
// return values. Each case previously emitted mismatched IR (verifier ICE or
// a silent bit-reinterpreting store under opaque pointers).
struct P {
    float x;
    double y;
}

impl P {
    fn scale(self, double k) -> double {
        return self.y * k;
    }
}

fn half(float f) -> float {
    return f / 2.0;
}

fn as_double(int i) -> double {
    return i;
}

fn main() -> int {
    println("call int->float: {:.2f}", half(3));
    println("return int->double: {:.2f}", as_double(4));

    P p = P { x: 1.0, y: 2.0 };
    p.x = 5;
    p.x += 1;
    p.y *= 3;
    p.y -= 0.5;
    println("fields: {:.2f} {:.2f}", p.x, p.y);
    println("method int->double: {:.2f}", p.scale(2));

    float f = 1.0;
    f += 1;
    double d = 2.0;
    d *= 3;
    d /= 4;
    long l = 10;
    l += 5;
    println("vars: {:.2f} {:.2f} {}", f, d, l);

    Array<float> a = [];
    a.push(2.5);
    a.push(3);
    println("push: {:.2f} {:.2f}", a[0], a[1]);
    return 0;
}
