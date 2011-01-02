
int foo( int y )
{
	int i = 5;
	int j = 15;
	return y + i * j;
}

int test_func( int x, int y )
{
	return foo( x );
}

