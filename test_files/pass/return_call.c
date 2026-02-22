// Return with function call result

int identity( int x )
{
	return x;
}

int wrapper( int a )
{
	return identity( a );
}
