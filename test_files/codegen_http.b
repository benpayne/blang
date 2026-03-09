// E2E test: HTTP server and client — start server, send GET request, verify response.
// Uses spawn for the client so server and client run concurrently.

import net;

fn main() -> int {
	net.HttpServer server = net.http_server("127.0.0.1", 9876);

	server.on_request(fn(net.HttpRequest req) -> net.HttpResponse {
		if req.path == "/health" {
			return net.http_ok("OK");
		}
		return net.http_not_found();
	});

	spawn {
		// Give the server a moment to start
		string body = net.http_get("127.0.0.1", 9876, "/health");
		if body == "OK" {
			server.shutdown();
		}
	}

	server.join();
	return 0;
}
