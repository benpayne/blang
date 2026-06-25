// Demo 13: HTTP Server with routing
//
// A simple HTTP server using the BLang networking library's route table.
// Registers handlers per method+path with .get()/.post(); unmatched
// requests get an automatic 404.
//
// Run: bcc demos/13_http_server.b -o http_server && ./http_server
// Test: curl http://127.0.0.1:8080/health
//       curl http://127.0.0.1:8080/hello
//       curl -X POST -d 'hi' http://127.0.0.1:8080/echo

import net;

fn main() -> int {
	int port = 8080;
	net.HttpServer server = net.http_server("0.0.0.0", port);

	server.get("/health", fn(net.HttpRequest req) -> net.HttpResponse {
		return net.http_ok("OK");
	});

	server.get("/hello", fn(net.HttpRequest req) -> net.HttpResponse {
		return net.http_ok("Hello from BLang!");
	});

	// Echo the request body back to the client.
	server.post("/echo", fn(net.HttpRequest req) -> net.HttpResponse {
		return net.http_ok(req.body);
	});

	server.serve();
	println("HTTP server listening on port {}...", port);

	server.join();
	return 0;
}
