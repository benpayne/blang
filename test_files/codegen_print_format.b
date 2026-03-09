// E2E test: print with format specifiers
// Tests: {:x} {:X} {:o} {:b} {:.2f} {:e}

fn main() -> int {
	int n = 255;
	double pi = 3.14159;

	// Hex
	println("hex: {:x}", n);
	println("HEX: {:X}", n);

	// Octal
	println("oct: {:o}", n);

	// Binary
	println("bin: {:b}", n);

	// Fixed-point
	println("fixed: {:.2f}", pi);
	println("fixed4: {:.4f}", pi);

	// Scientific
	println("sci: {:e}", pi);

	// Mixed
	println("val={} hex={:x} fixed={:.2f}", n, n, pi);

	println("print format codegen test passed!");
	return 0;
}
