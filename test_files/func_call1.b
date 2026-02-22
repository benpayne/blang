
fn foo( int y ) -> int
{
	int i = 5;
	int j = 15;
	return y + i * j;
}

fn test_func( int x, int y ) -> int
{
	return foo( x );
}

