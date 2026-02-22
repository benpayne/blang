// Return with function call result

fn identity( int x ) -> int
{
	return x;
}

fn wrapper( int a ) -> int
{
	return identity( a );
}
