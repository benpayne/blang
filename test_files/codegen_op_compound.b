// Operator matrix (functional-hardening U2 / REQ-002): the supported extended
// compound assignments %= and ^= (plus += for context), applied repeatedly.
// Printed AND asserted.

fn main() -> int {
	// %= : remainder-assign.
	int a = 17;
	a %= 5;              // 2
	println("a1 {}", a);
	assert a == 2, "17 %= 5";
	a += 10;             // 12
	a %= 7;              // 5
	println("a2 {}", a);
	assert a == 5, "12 %= 7";

	// ^= : xor-assign.
	int b = 12;
	b ^= 10;             // 1100 ^ 1010 = 0110 = 6
	println("b1 {}", b);
	assert b == 6, "12 ^= 10";
	b ^= 6;              // 6 ^ 6 = 0
	println("b2 {}", b);
	assert b == 0, "6 ^= 6";
	b ^= 255;            // 0 ^ 255 = 255
	println("b3 {}", b);
	assert b == 255, "0 ^= 255";

	// Chained updates interleaving += and %=.
	int c = 3;
	c += 4;              // 7
	c %= 4;              // 3
	c ^= 1;              // 2
	println("c {}", c);
	assert c == 2, "chained compound";

	println("PASS");
	return 0;
}
