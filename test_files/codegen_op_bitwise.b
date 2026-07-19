// Operator matrix (functional-hardening U2 / REQ-002): bitwise AND/OR/XOR and
// unary complement on int, plus precedence with + and ==. Every result is
// printed (golden) AND asserted. Decimal literals only (hex is unsupported).

fn main() -> int {
	int a = 12;   // 1100
	int b = 10;   // 1010

	int band = a & b;   // 1000 = 8
	int bor  = a | b;   // 1110 = 14
	int bxor = a ^ b;   // 0110 = 6
	println("and {}", band);
	println("or {}", bor);
	println("xor {}", bxor);
	assert band == 8, "and";
	assert bor == 14, "or";
	assert bxor == 6, "xor";

	// Unary complement.
	int comp = ~a;      // ~12 = -13
	println("not {}", comp);
	assert comp == -13, "complement";

	// Masking idiom (decimal masks): keep low 4 bits of 250 (11111010) -> 10.
	int masked = 250 & 15;
	println("mask {}", masked);
	assert masked == 10, "mask";

	// Precedence: & binds tighter than ==, and + tighter than &.
	// 1 + 2 & 3  ==  (1+2) & 3  ==  3 & 3  ==  3
	int prec = 1 + 2 & 3;
	println("prec {}", prec);
	assert prec == 3, "precedence + over &";

	// C precedence footgun: == binds TIGHTER than &, so
	//   a & b == 8  parses as  a & (b == 8)  ==  12 & 0  == 0 (false),
	// while the parenthesized (a & b) == 8 is true.
	bool cmp_unparen = a & b == 8;
	bool cmp_paren = ( a & b ) == 8;
	println("cmp_unparen {}", cmp_unparen);
	println("cmp_paren {}", cmp_paren);
	assert cmp_unparen == false, "== binds tighter than &";
	assert cmp_paren == true, "(a & b) == 8";

	println("PASS");
	return 0;
}
