extern fn printf(cstring fmt, ...) -> int;

fn negate(int x) -> int
{
	return -x;
}

fn bitflip(int x) -> int
{
	return ~x;
}

fn main() -> int
{
	// Test unary negation
	int a = -5;
	int b = negate(a);

	// Test logical not
	int c = 0;
	int d = !c;

	// Test bitwise not
	int e = bitflip(0);

	// Test nested function calls: negate(negate(10)) == 10
	int f = negate(negate(10));

	// Test string escape sequences
	printf("hello\tworld\n");
	printf("a=%d b=%d d=%d e=%d f=%d\n", a, b, d, e, f);

	// a=-5, b=5, d=1(from !0), e=-1(from ~0), f=10(from negate(negate(10)))
	// sum = -5 + 5 + 1 + -1 + 10 = 10
	int sum = a + b + d + e + f;
	printf("sum = %d\n", sum);
	if sum == 10 {
		return 0;
	}
	return 1;
}
