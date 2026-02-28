// Nested if/else statements

fn action( int x ) -> int
{
	return x;
}

fn run_test( int a, int b )
{
	if a {
		if b {
			action( a );
		} else {
			action( b );
		}
	}
}
