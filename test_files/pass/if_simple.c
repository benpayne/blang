// Simple if/else statements with variable conditions

int check( int x )
{
	return x;
}

void test( int a )
{
	if ( a )
		check( a );

	if ( a )
	{
		check( a );
	}

	if ( a )
	{
		check( a );
	}
	else
	{
		check( a );
	}

	if ( a )
		check( a );
	else
		check( a );
}
