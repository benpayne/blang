// Stdlib-via-bcc (functional-hardening U4 / REQ-004): net PURE HTTP helpers
// (no socket) exercised THROUGH THE REAL bcc DRIVER, with printed goldens.
// These helpers are covered by codegen_http_blang.b / codegen_http_routing.b via
// qcc --combine, but test_codegen.sh never invokes bcc — so "compiles+runs
// through bcc" (D4) is the new signal here. All deterministic; printed AND
// asserted.

import net;

fn home_handler( net.HttpRequest req ) -> net.HttpResponse {
	return net.http_ok("home");
}

fn main() -> int {
	// http_status_text over several codes.
	println("s200={}", net.http_status_text(200));
	println("s404={}", net.http_status_text(404));
	println("s500={}", net.http_status_text(500));
	assert net.http_status_text(200) == "OK", "200";
	assert net.http_status_text(404) == "Not Found", "404";
	assert net.http_status_text(500) == "Internal Server Error", "500";

	// build_http_response — check structure via substrings (avoids CRLF in golden).
	string resp = net.build_http_response(200, "text/plain", "hi");
	println("resp_starts={}", resp.starts_with("HTTP/1.1 200 OK"));
	println("resp_has_ct={}", resp.contains("Content-Type: text/plain"));
	println("resp_has_len={}", resp.contains("Content-Length: 2"));
	println("resp_ends_body={}", resp.ends_with("hi"));
	assert resp.starts_with("HTTP/1.1 200 OK"), "status line";
	assert resp.contains("Content-Length: 2"), "content length";
	assert resp.ends_with("hi"), "body";

	// parse_http_request_line — build a request line + CRLF in a Buffer.
	Buffer rl = Buffer.from_string("GET /index.html HTTP/1.1");
	rl.append_byte(13);
	rl.append_byte(10);
	net.HttpRequestLine parsed = net.parse_http_request_line(rl);
	println("method={}", parsed.method);
	println("path={}", parsed.path);
	println("version={}", parsed.version);
	assert parsed.method == "GET", "parsed method";
	assert parsed.path == "/index.html", "parsed path";
	assert parsed.version == "HTTP/1.1", "parsed version";

	// dispatch_request with a printed golden (complements the exit-code-only
	// codegen_http_routing.b) — matched route + 404 fallthrough.
	Array<net.Route> routes = [];
	routes.push(net.Route { method: "GET", path: "/", handler: home_handler });
	net.HttpResponse hit = net.dispatch_request(routes,
		net.HttpRequest { method: "GET", path: "/", body: "" });
	net.HttpResponse miss = net.dispatch_request(routes,
		net.HttpRequest { method: "GET", path: "/nope", body: "" });
	println("hit_status={} hit_body={}", hit.status, hit.body);
	println("miss_status={}", miss.status);
	assert hit.status == 200, "hit status";
	assert hit.body == "home", "hit body";
	assert miss.status == 404, "miss 404";

	println("PASS");
	return 0;
}
