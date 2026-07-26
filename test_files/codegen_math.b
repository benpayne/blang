// U4: math module via bcc import — also the first float/double codegen test.
// Deterministic: fixed inputs -> golden-stable output.
import math;

fn main() -> int {
    double r = math.sqrt(144.0);
    double p = math.pow(2.0, 8.0);
    double f = math.floor(3.9);
    double c = math.ceil(3.1);
    double a = math.fabs(-2.5);
    int ai = math.abs_int(-42);
    double pi = math.pi();
    println("sqrt(144) = {:.1f}", r);
    println("pow(2,8) = {:.1f}", p);
    println("floor(3.9) = {:.1f}", f);
    println("ceil(3.1) = {:.1f}", c);
    println("fabs(-2.5) = {:.1f}", a);
    println("abs_int(-42) = {}", ai);
    println("pi ~ {:.4f}", pi);
    return 0;
}
