// Operator matrix (functional-hardening U2 / REQ-002): `byte` is UNSIGNED
// (0-255). Bitwise ops and right-shift on bytes stay unsigned, and a byte
// prints unsigned via the {} builtin. Results are routed through byte variables
// (the form the compiler tracks as byte) then printed AND asserted.
//
// Pre-fix, `byte 200 >> 1` printed -28 (arithmetic shift of signed -56) and a
// byte >= 128 printed as a negative i8; post-fix they are unsigned.

fn main() -> int {
	byte hi = 240;   // 11110000
	byte lo = 15;    // 00001111

	byte b_or  = hi | lo;   // 11111111 = 255
	byte b_and = hi & lo;   // 00000000 = 0
	byte b_xor = hi ^ lo;   // 11111111 = 255
	println("or {}", b_or);
	println("and {}", b_and);
	println("xor {}", b_xor);
	assert b_or == 255, "byte or";
	assert b_and == 0, "byte and";
	assert b_xor == 255, "byte xor";

	// Unsigned right shift: 200 (11001000) >> 1 = 100 (01100100).
	byte s = 200;
	byte sh = s >> 1;
	println("shr {}", sh);
	assert sh == 100, "byte unsigned shr";

	// A byte with the high bit set prints unsigned (not -1).
	byte full = 255;
	println("full {}", full);
	assert full == 255, "byte 255 prints unsigned";

	// Left shift within byte range.
	byte one = 1;
	byte shifted = one << 6;   // 64
	println("shl {}", shifted);
	assert shifted == 64, "byte shl";

	println("PASS");
	return 0;
}
