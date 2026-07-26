// Test: byte type — 8-bit unsigned integer

extern fn printf(cstring fmt, ...) -> int;

fn main() -> int {
	// Basic byte variable
	byte b = 42;
	int val = b;
	if val != 42 { printf("FAIL: byte value %d != 42\n", val); return 1; }

	// Byte in struct
	byte high = 255;
	int hval = high;
	if hval != 255 { printf("FAIL: byte 255 got %d\n", hval); return 1; }

	// Byte in Array
	Array<byte> data = [10, 20, 30];
	byte first = data[0];
	int fval = first;
	if fval != 10 { printf("FAIL: Array<byte>[0] got %d\n", fval); return 1; }

	byte second = data[1];
	int sval = second;
	if sval != 20 { printf("FAIL: Array<byte>[1] got %d\n", sval); return 1; }

	// Array<byte> length
	int len = data.length;
	if len != 3 { printf("FAIL: Array<byte>.length got %d\n", len); return 1; }

	// Byte push
	data.push(40);
	int len2 = data.length;
	if len2 != 4 { printf("FAIL: after push length got %d\n", len2); return 1; }

	printf("All byte tests passed!\n");
	return 0;
}
