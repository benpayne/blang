extern fn __blang_buffer_create(long capacity) -> Buffer;
extern fn __blang_buffer_create_from_string(string s) -> Buffer;

fn main() -> int {
    // Create a buffer
    Buffer buf = __blang_buffer_create(64);

    // Test empty state
    if buf.length != 0 { return 1; }
    if buf.is_empty != 1 { return 2; }

    // Append bytes
    buf.append_byte(72);
    buf.append_byte(101);
    buf.append_byte(108);
    buf.append_byte(108);
    buf.append_byte(111);

    if buf.length != 5 { return 3; }
    if buf.is_empty != 0 { return 4; }

    // Read bytes
    if buf.get(0) != 72 { return 5; }
    if buf.get(4) != 111 { return 6; }

    // Set a byte
    buf.set(0, 104);
    if buf.get(0) != 104 { return 7; }

    // Convert to string
    string s = buf.to_string();
    if s != "hello" { return 8; }

    // Create from string
    Buffer buf2 = __blang_buffer_create_from_string("world");
    if buf2.length != 5 { return 9; }

    string s2 = buf2.to_string();
    if s2 != "world" { return 10; }

    // Clear
    buf.clear();
    if buf.length != 0 { return 11; }

    // Append string
    buf.append_string("test");
    if buf.length != 4 { return 12; }
    string s3 = buf.to_string();
    if s3 != "test" { return 13; }

    // Index of
    buf.clear();
    buf.append_string("hello\r\n\r\nworld");
    Buffer pattern = __blang_buffer_create(4);
    pattern.append_byte(13);
    pattern.append_byte(10);
    pattern.append_byte(13);
    pattern.append_byte(10);
    int pos = buf.index_of(pattern, 0);
    if pos != 5 { return 14; }

    // Slice
    Buffer head = buf.slice(0, 5);
    string headStr = head.to_string();
    if headStr != "hello" { return 15; }

    // to_string_range
    string body = buf.to_string_range(9, 14);
    if body != "world" { return 16; }

    println("Buffer test passed!");
    return 0;
}
