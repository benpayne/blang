// Binary expression codegen test: arithmetic, comparisons, function calls in expressions
// Expected: main returns 80 (5 + 15 * 5 = 80)

int multiply( int a, int b )
{
	return a * b;
}

int add( int a, int b )
{
	return a + b;
}

int main()
{
	int x = 5;
	int y = 15;
	int z = add( x, multiply( y, x ) );
	return z;
}
