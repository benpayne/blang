
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
	int i = 5;
	
	char c = '\0';
	//c = 0x20;
	c = 'a';
	
	char a = 0xf;
	int b = 4, c;
	
	c = a + b;
	c = a << b;
	
	// block 0a
	if ( c )
		printf( "a" );

	// block 0b
	if ( c )
	{
		printf( "a" );
	}

	// block 0c
	if ( c == 0 )
	{
		printf( "a" );
	}
	else
		printf( "b" );
		
	// block 1
	if ( c )
	{
		if ( a > 0 )
			printf( "a" );
		else
			printf( "b" );
	}
	
	// block 2
	if ( c )
		if ( a < 0 )
			printf( "a" );
		else
			printf( "b" );

	test_func( b, c );
	
	string s = "help";
	
	printf( "Hello World %d\n", i );
	
	
	return i;
}


