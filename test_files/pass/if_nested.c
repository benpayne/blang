// Nested if/else statements

int action( int x )
{
	return x;
}

void test( int a, int b )
{
	if ( a )
	{
		if ( b )
			action( a );
		else
			action( b );
	}

	if ( a )
		if ( b )
			action( a );
		else
			action( b );
}
