extern fn printf(cstring fmt, ...) -> int;

fn main()
{
	int returnType = 10;
	printf( "Hello World\n", returnType );
}
