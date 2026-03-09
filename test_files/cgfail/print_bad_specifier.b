// Fail test: hex specifier on float type

fn main() -> int {
	float pi = 3.14;
	println("{:x}", pi);
	return 0;
}
