
int foo( int y )
{
	return y;
}

void test_func( int x, int y )
{
	foo( x );
}

// my main function
int main()
{
	// this is a comment
	int intFive = 5;
	
	char _c = '\0';
	//c = 0x20;
	_c = 'a';
	
	char a = 0xf;
	int b = 4, c;
	
	_c = a + b;
	_c = a << b;
	
	// block 0a
	if ( _c )
		printf( "a" );

	// block 0b
	if ( _c )
	{
		printf( "a" );
	}

	// block 0c
	if ( _c == 0 )
	{
		printf( "a" );
	}
	else
		printf( "b" );
		
	// block 1
	if ( _c )
	{
		if ( a > 0 )
			printf( "a" );
		else
			printf( "b" );
	}
	
	// block 2
	if ( _c )
		if ( a < 0 )
			printf( "a" );
		else
			printf( "b" );

	test_func( b, _c );
	
	string s = "help";
	
	printf( "Hello World %d\n", intFive );
	
	
	return i;
}


