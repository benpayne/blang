// E2E test: BLang-native HTTP parsing and response building (Phase 6)
// Tests pure-BLang HTTP functions that use Buffer for parsing and
// string interpolation for response construction.

import net;

extern fn __blang_buffer_create(long capacity) -> Buffer;
extern fn __blang_buffer_create_from_string(string s) -> Buffer;

fn main() -> int {
	// ---- Test 1: build_http_response ----
	string resp = net.build_http_response(200, "text/plain", "Hello!");
	if resp.starts_with("HTTP/1.1 200 OK") != true { return 1; }
	if resp.contains("Content-Type: text/plain") != true { return 2; }
	if resp.contains("Content-Length: 6") != true { return 3; }
	if resp.contains("Connection: close") != true { return 4; }
	if resp.ends_with("Hello!") != true { return 5; }

	// ---- Test 2: build_http_response with 404 ----
	string resp404 = net.build_http_response(404, "text/html", "Not Found");
	if resp404.starts_with("HTTP/1.1 404 Not Found") != true { return 6; }
	if resp404.contains("Content-Length: 9") != true { return 7; }

	// ---- Test 3: http_status_text ----
	if net.http_status_text(200) != "OK" { return 10; }
	if net.http_status_text(404) != "Not Found" { return 11; }
	if net.http_status_text(500) != "Internal Server Error" { return 12; }
	if net.http_status_text(999) != "Unknown" { return 13; }

	// ---- Test 4: parse_http_request_line ----
	Buffer req_buf = __blang_buffer_create(256);
	req_buf.append_string("GET /hello HTTP/1.1\r\nHost: localhost\r\n\r\n");

	net.HttpRequestLine reqline = net.parse_http_request_line(req_buf);
	if reqline.method != "GET" { return 20; }
	if reqline.path != "/hello" { return 21; }
	if reqline.version != "HTTP/1.1" { return 22; }

	// ---- Test 5: parse POST request line ----
	Buffer post_buf = __blang_buffer_create(256);
	post_buf.append_string("POST /api/data HTTP/1.1\r\nContent-Type: application/json\r\n\r\n");

	net.HttpRequestLine postline = net.parse_http_request_line(post_buf);
	if postline.method != "POST" { return 23; }
	if postline.path != "/api/data" { return 24; }

	// ---- Test 6: parse_http_headers_from_buffer ----
	Buffer hdr_buf = __blang_buffer_create(512);
	hdr_buf.append_string("GET / HTTP/1.1\r\nHost: example.com\r\nContent-Type: text/plain\r\nX-Custom: hello\r\n\r\n");

	net.HttpParsedHeaders hdrs = net.parse_http_headers_from_buffer(hdr_buf);
	if hdrs.keys.length != 3 { return 30; }
	if hdrs.keys[0] != "Host" { return 31; }
	if hdrs.values[0] != "example.com" { return 32; }
	if hdrs.keys[1] != "Content-Type" { return 33; }
	if hdrs.values[1] != "text/plain" { return 34; }
	if hdrs.keys[2] != "X-Custom" { return 35; }
	if hdrs.values[2] != "hello" { return 36; }

	// ---- Test 7: extract_http_body ----
	Buffer body_buf = __blang_buffer_create(256);
	body_buf.append_string("HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nhello");

	string body = net.extract_http_body(body_buf);
	if body != "hello" { return 40; }

	// ---- Test 8: extract_http_body with empty body ----
	Buffer empty_body_buf = __blang_buffer_create(256);
	empty_body_buf.append_string("HTTP/1.1 204 No Content\r\n\r\n");

	string empty_body = net.extract_http_body(empty_body_buf);
	if empty_body != "" { return 41; }

	// ---- Test 9: parse request line with bad input ----
	Buffer bad_buf = __blang_buffer_create(32);
	bad_buf.append_string("garbage");

	net.HttpRequestLine badline = net.parse_http_request_line(bad_buf);
	if badline.method != "" { return 50; }
	if badline.path != "" { return 51; }

	println("HTTP BLang-native test passed!");
	return 0;
}
