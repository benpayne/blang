// Multiple function definitions with cross-calls

fn first( int x ) -> int
{
	return x;
}

fn second( int a, int b ) -> int
{
	first( a );
	return b;
}

fn third( int a, int b )
{
	first( a );
	second( a, b );
}
