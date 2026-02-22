extern fn printf(string, ...) -> int;
extern fn exit(int);
extern fn add(int, int) -> int;
extern fn mixed(int a, string, int c) -> int;

fn main() -> int
{
	printf("hello world\n");
	return 0;
}
