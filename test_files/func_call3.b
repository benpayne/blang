extern fn printf(string fmt, ...) -> int;

fn main()
{
	int returnType = 10;
	printf( "Hello World\n", returnType );
}
