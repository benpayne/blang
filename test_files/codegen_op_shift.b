// Operator matrix (functional-hardening U2 / REQ-002): left/right shift on int,
// including a negative LHS (arithmetic right shift preserves sign) and building
// a large power of two. Printed (golden) AND asserted.

fn main() -> int {
	int a = 12;

	int shl = a << 2;    // 12 * 4 = 48
	int shr = a >> 1;    // 12 / 2 = 6
	println("shl {}", shl);
	println("shr {}", shr);
	assert shl == 48, "shl";
	assert shr == 6, "shr";

	// Negative LHS: arithmetic right shift preserves the sign bit.
	int neg = -8;
	int negshr = neg >> 1;   // -4
	println("negshr {}", negshr);
	assert negshr == -4, "arithmetic shr on negative int";

	// Build a large power of two.
	int big = 1 << 30;       // 1073741824
	println("big {}", big);
	assert big == 1073741824, "1 << 30";

	// Shift inside a larger expression (precedence: << lower than +).
	int mixed = 1 + 1 << 3;  // (1+1) << 3 = 16
	println("mixed {}", mixed);
	assert mixed == 16, "shift precedence";

	println("PASS");
	return 0;
}
