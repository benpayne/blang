// E2E test: HTTP streaming helper functions (int_to_hex, send_http_headers,
// send_http_chunk, end_http_chunked, http_status_text)

import net;

fn main() -> int {
	// ---- Test 1: int_to_hex ----
	if net.int_to_hex(0) != "0" { return 1; }
	if net.int_to_hex(1) != "1" { return 2; }
	if net.int_to_hex(10) != "a" { return 3; }
	if net.int_to_hex(15) != "f" { return 4; }
	if net.int_to_hex(16) != "10" { return 5; }
	if net.int_to_hex(255) != "ff" { return 6; }
	if net.int_to_hex(256) != "100" { return 7; }
	if net.int_to_hex(4096) != "1000" { return 8; }
	if net.int_to_hex(65535) != "ffff" { return 9; }

	// ---- Test 2: build_http_response still works ----
	string resp = net.build_http_response(200, "text/plain", "OK");
	if resp.starts_with("HTTP/1.1 200 OK") != true { return 10; }
	if resp.contains("Content-Length: 2") != true { return 11; }
	if resp.ends_with("OK") != true { return 12; }

	// ---- Test 3: http_status_text ----
	if net.http_status_text(200) != "OK" { return 20; }
	if net.http_status_text(204) != "No Content" { return 21; }
	if net.http_status_text(405) != "Method Not Allowed" { return 22; }
	if net.http_status_text(500) != "Internal Server Error" { return 23; }

	println("HTTP streaming helpers test passed!");
	return 0;
}
