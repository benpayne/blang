// Function calls as statements

fn add( int x ) -> int
{
	return x;
}

fn caller( int a, int b )
{
	add( a );
	add( b );
}
