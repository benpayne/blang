// Operator matrix (functional-hardening U2 / REQ-002): the % (remainder)
// operator on int, including negative dividends (C truncated-division
// semantics: the result takes the sign of the dividend). Printed AND asserted.

fn main() -> int {
	int m1 = 12 % 10;    // 2
	int m2 = 17 % 5;     // 2
	int m3 = 20 % 4;     // 0 (exact)
	println("m1 {}", m1);
	println("m2 {}", m2);
	println("m3 {}", m3);
	assert m1 == 2, "12 % 10";
	assert m2 == 2, "17 % 5";
	assert m3 == 0, "20 % 4";

	// Negative dividend: C truncated remainder keeps the dividend's sign.
	int neg = -7 % 3;    // -1
	println("neg {}", neg);
	assert neg == -1, "-7 % 3";

	// Negative divisor: sign follows the dividend, not the divisor.
	int negd = 7 % -3;   // 1
	println("negd {}", negd);
	assert negd == 1, "7 % -3";

	// % inside a larger expression (precedence: % same tier as * and /).
	int expr = 2 + 14 % 4;   // 2 + (14 % 4) = 2 + 2 = 4
	println("expr {}", expr);
	assert expr == 4, "modulo precedence";

	println("PASS");
	return 0;
}
