// Comparison in if condition - not yet supported by parser
// Parser only handles simple variable references as conditions

int check( int x )
{
	return x;
}

void test( int a )
{
	if ( a == 0 )
		check( a );
}
