// Test: Buffer type (pure BLang, backed by Array<byte>)

extern fn printf(cstring fmt, ...) -> int;

fn main() -> int {
	// Create a buffer
	Buffer buf = Buffer(64);

	// Test empty state
	int len = buf.get_length();
	if len != 0 { printf("FAIL: initial length %d != 0\n", len); return 1; }

	// Append bytes
	buf.append_byte(72);
	buf.append_byte(101);
	buf.append_byte(108);
	buf.append_byte(108);
	buf.append_byte(111);

	len = buf.get_length();
	if len != 5 { printf("FAIL: length after append %d != 5\n", len); return 3; }

	// Read bytes
	int b0 = buf.get(0);
	if b0 != 72 { printf("FAIL: buf.get(0) = %d != 72\n", b0); return 5; }
	int b4 = buf.get(4);
	if b4 != 111 { printf("FAIL: buf.get(4) = %d != 111\n", b4); return 6; }

	// Set a byte
	buf.set(0, 104);
	b0 = buf.get(0);
	if b0 != 104 { printf("FAIL: buf.get(0) after set = %d != 104\n", b0); return 7; }

	// Convert to string
	string s = buf.to_string();
	if s != "hello" { printf("FAIL: to_string != hello\n"); return 8; }

	// Create from string
	Buffer buf2 = Buffer.from_string("world");
	int len2 = buf2.get_length();
	if len2 != 5 { printf("FAIL: from_string length %d != 5\n", len2); return 9; }

	string s2 = buf2.to_string();
	if s2 != "world" { printf("FAIL: from_string to_string != world\n"); return 10; }

	// Clear
	buf.clear();
	len = buf.get_length();
	if len != 0 { printf("FAIL: length after clear %d != 0\n", len); return 11; }

	// Append string
	buf.append_string("test");
	len = buf.get_length();
	if len != 4 { printf("FAIL: length after append_string %d != 4\n", len); return 12; }
	string s3 = buf.to_string();
	if s3 != "test" { printf("FAIL: append_string to_string != test\n"); return 13; }

	// Index of
	buf.clear();
	buf.append_string("hello\r\n\r\nworld");
	Buffer pattern = Buffer(4);
	pattern.append_byte(13);
	pattern.append_byte(10);
	pattern.append_byte(13);
	pattern.append_byte(10);
	int pos = buf.index_of(pattern, 0);
	if pos != 5 { printf("FAIL: index_of = %d != 5\n", pos); return 14; }

	// Slice
	Buffer head = buf.slice(0, 5);
	string headStr = head.to_string();
	if headStr != "hello" { printf("FAIL: slice to_string != hello\n"); return 15; }

	// to_string_range
	string body = buf.to_string_range(9, 14);
	if body != "world" { printf("FAIL: to_string_range != world\n"); return 16; }

	println("Buffer test passed!");
	return 0;
}
