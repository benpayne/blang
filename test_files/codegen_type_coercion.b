// E2E test: integer type coercion in binary expressions

fn main() -> int {
	// char (i8) compared with int literal (i32)
	char c = 65;
	if c != 65 { return 1; }
	if c == 0 { return 2; }

	// short (i16) compared with int literal (i32)
	short s = 100;
	if s != 100 { return 3; }
	if s < 50 { return 4; }

	// long (i64) compared with int literal (i32)
	long l = 999999;
	if l != 999999 { return 5; }
	if l <= 0 { return 6; }

	// bool (i1) compared with int (true != 0)
	bool b = true;
	if b != true { return 7; }

	// Mixed arithmetic: short + int
	int si = s + 42;
	if si != 142 { return 8; }

	// Mixed arithmetic: char + int
	int ci = c + 1;
	if ci != 66 { return 9; }

	// Mixed arithmetic: long + int
	long li = l + 1;
	if li != 1000000 { return 10; }

	// Float/double mixed operations
	float f = 3.14;
	double d = 2.718;

	println("Type coercion test passed!");
	return 0;
}
