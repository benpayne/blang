// Binary expression codegen test: arithmetic, comparisons, function calls in expressions
// Expected: z == 80 (5 + 15 * 5 = 80)

extern fn printf(cstring fmt, ...) -> int;

fn multiply( int a, int b ) -> int
{
	return a * b;
}

fn add( int a, int b ) -> int
{
	return a + b;
}

fn main() -> int
{
	int x = 5;
	int y = 15;
	int z = add( x, multiply( y, x ) );
	printf("z = %d\n", z);
	if z == 80 {
		return 0;
	}
	return 1;
}
