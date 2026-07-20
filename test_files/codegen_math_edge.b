// U6: math log/exp/pow/sqrt edge values. Deterministic golden.
import math;
fn main() -> int {
    println("log(1) = {:.4f}", math.log(1.0));
    println("log10(1000) = {:.4f}", math.log10(1000.0));
    println("exp(0) = {:.4f}", math.exp(0.0));
    println("pow(3,3) = {:.1f}", math.pow(3.0, 3.0));
    println("sqrt(625) = {:.1f}", math.sqrt(625.0));
    println("exp(log(5)) = {:.4f}", math.exp(math.log(5.0)));
    return 0;
}
