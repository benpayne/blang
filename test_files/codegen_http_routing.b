// E2E test for HTTP routing: route table dispatch and .get()/.post()
// registration. Exercises the routing logic without a live socket by calling
// dispatch_request directly and via a server's registered routes.

import net;

fn main() -> int {
	// ---- Part 1: dispatch_request against a manually built route table ----
	Array<net.Route> routes = [];
	routes.push(net.Route {
		method: "GET", path: "/health",
		handler: fn(net.HttpRequest req) -> net.HttpResponse { return net.http_ok("OK"); }
	});
	routes.push(net.Route {
		method: "GET", path: "/hello",
		handler: fn(net.HttpRequest req) -> net.HttpResponse { return net.http_ok("hi"); }
	});
	routes.push(net.Route {
		method: "POST", path: "/data",
		handler: fn(net.HttpRequest req) -> net.HttpResponse { return net.http_json("{}"); }
	});

	net.HttpResponse r1 = net.dispatch_request(routes,
		net.HttpRequest { method: "GET", path: "/health", body: "" });
	if r1.status != 200 { return 1; }
	if r1.body != "OK" { return 2; }

	net.HttpResponse r2 = net.dispatch_request(routes,
		net.HttpRequest { method: "GET", path: "/hello", body: "" });
	if r2.body != "hi" { return 3; }

	net.HttpResponse r3 = net.dispatch_request(routes,
		net.HttpRequest { method: "POST", path: "/data", body: "x" });
	if r3.content_type != "application/json" { return 4; }

	// Unknown path -> 404
	net.HttpResponse r4 = net.dispatch_request(routes,
		net.HttpRequest { method: "GET", path: "/missing", body: "" });
	if r4.status != 404 { return 5; }

	// Right path, wrong method -> 404 (only POST /data is registered)
	net.HttpResponse r5 = net.dispatch_request(routes,
		net.HttpRequest { method: "GET", path: "/data", body: "" });
	if r5.status != 404 { return 6; }

	// ---- Part 2: register routes via HttpServer.get/.post (no socket) ----
	Array<net.Route> empty = [];
	net.HttpServer srv = net.HttpServer {
		_selector_handle: 0, _server_fd: 0, _routes: empty
	};
	srv.get("/ping", fn(net.HttpRequest req) -> net.HttpResponse {
		return net.http_ok("pong");
	});
	srv.post("/echo", fn(net.HttpRequest req) -> net.HttpResponse {
		return net.http_ok(req.body);
	});

	net.HttpResponse p1 = net.dispatch_request(srv._routes,
		net.HttpRequest { method: "GET", path: "/ping", body: "" });
	if p1.body != "pong" { return 7; }

	net.HttpResponse p2 = net.dispatch_request(srv._routes,
		net.HttpRequest { method: "POST", path: "/echo", body: "payload" });
	if p2.body != "payload" { return 8; }

	println("HTTP routing test passed!");
	return 0;
}
