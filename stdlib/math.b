// stdlib/math.b — Math module: libm-backed floating-point functions.
//
// Usage: import math;
//   math.sqrt(x), math.pow(b,e), math.sin/cos/tan(x), math.log/log10/exp(x),
//   math.floor/ceil/fabs(x)  -> double
//   math.abs_int(n)          -> int
//   math.pi(), math.e()      -> double (constants as functions; module-level
//                               globals are not yet a codegen feature)

extern fn __blang_math_sqrt(double x) -> double;
extern fn __blang_math_pow(double base, double exp) -> double;
extern fn __blang_math_sin(double x) -> double;
extern fn __blang_math_cos(double x) -> double;
extern fn __blang_math_tan(double x) -> double;
extern fn __blang_math_log(double x) -> double;
extern fn __blang_math_log10(double x) -> double;
extern fn __blang_math_exp(double x) -> double;
extern fn __blang_math_floor(double x) -> double;
extern fn __blang_math_ceil(double x) -> double;
extern fn __blang_math_fabs(double x) -> double;
extern fn __blang_math_abs_int(int x) -> int;

pub fn sqrt(double x) -> double { return __blang_math_sqrt(x); }
pub fn pow(double base, double exp) -> double { return __blang_math_pow(base, exp); }
pub fn sin(double x) -> double { return __blang_math_sin(x); }
pub fn cos(double x) -> double { return __blang_math_cos(x); }
pub fn tan(double x) -> double { return __blang_math_tan(x); }
pub fn log(double x) -> double { return __blang_math_log(x); }
pub fn log10(double x) -> double { return __blang_math_log10(x); }
pub fn exp(double x) -> double { return __blang_math_exp(x); }
pub fn floor(double x) -> double { return __blang_math_floor(x); }
pub fn ceil(double x) -> double { return __blang_math_ceil(x); }
pub fn fabs(double x) -> double { return __blang_math_fabs(x); }
pub fn abs_int(int x) -> int { return __blang_math_abs_int(x); }

pub fn pi() -> double { return 3.14159265358979323846; }
pub fn e() -> double { return 2.71828182845904523536; }
