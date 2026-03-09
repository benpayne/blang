// Test: print with format specifiers

fn main() -> int {
	double pi = 3.14159;
	int n = 255;

	println("{:.2f} {:x}", pi, n);
	println("{:X}", n);
	println("{:o}", n);
	println("{:b}", n);
	println("{:e}", pi);
	return 0;
}
