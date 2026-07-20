// U6: math trig + identities. Deterministic golden.
import math;
fn main() -> int {
    println("sin(0) = {:.4f}", math.sin(0.0));
    println("cos(0) = {:.4f}", math.cos(0.0));
    println("tan(0) = {:.4f}", math.tan(0.0));
    // sin^2 + cos^2 = 1 at an arbitrary angle
    double s = math.sin(1.0);
    double c = math.cos(1.0);
    println("sin^2+cos^2(1) = {:.4f}", s * s + c * c);
    return 0;
}
