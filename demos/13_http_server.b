// Demo 13: HTTP Server
//
// A simple HTTP server using the BLang networking library.
// Handles GET /health (returns "OK") and GET /hello (returns greeting).
// All other paths return 404.
//
// Run: bcc demos/13_http_server.b -o http_server && ./http_server
// Test: curl http://127.0.0.1:8080/health
//       curl http://127.0.0.1:8080/hello

import net;

fn main() -> int {
	int port = 8080;
	net.HttpServer server = net.http_server("0.0.0.0", port);

	println("HTTP server listening on port {}...", port);

	server.on_request(fn(net.HttpRequest req) -> net.HttpResponse {
		println("{} {}", req.method, req.path);

		if req.path == "/health" {
			return net.http_ok("OK");
		}
		if req.path == "/hello" {
			return net.http_ok("Hello from BLang!");
		}
		return net.http_not_found();
	});

	server.join();
	return 0;
}
