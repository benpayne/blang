// Multiple function definitions with cross-calls

int first( int x )
{
	return x;
}

int second( int a, int b )
{
	first( a );
	return b;
}

void third( int a, int b )
{
	first( a );
	second( a, b );
}
