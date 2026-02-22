// Function calls as statements

int add( int x )
{
	return x;
}

void caller( int a, int b )
{
	add( a );
	add( b );
}
