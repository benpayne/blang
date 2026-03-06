// Test calling external functions with proper extern declaration

extern fn printf(cstring, ...) -> int;

fn main()
{
	printf( "Hello World\n" );
}
