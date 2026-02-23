// All ownership qualifiers in one function

fn main() -> int {
	own int a = 1;
	shared int b = 2;
	sync int c = 3;
	int d = 4;
	return 0;
}
