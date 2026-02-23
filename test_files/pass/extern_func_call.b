// Test calling external functions with proper extern declaration

extern fn printf(string, ...) -> int;

fn main()
{
	printf( "Hello World\n" );
}
