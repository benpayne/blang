// Auto-JSON HTTP response: net.http_json(to_json(struct)) serializes a @json
// struct into an application/json response body. This is the composition that
// gives HTTP handlers automatic JSON serialization.

import net;

@json
struct User {
	int id;
	string name;
}

fn user_handler(net.HttpRequest req) -> net.HttpResponse {
	User u = User { id: 7, name: "ada" };
	return net.http_json(to_json(u));
}

fn main() -> int {
	Array<net.Route> routes = [];
	routes.push(net.Route { method: "GET", path: "/user", handler: user_handler });

	net.HttpResponse resp = net.dispatch_request(routes,
		net.HttpRequest { method: "GET", path: "/user", body: "" });

	if resp.status != 200 { return 1; }
	if resp.content_type != "application/json" { return 2; }
	if resp.body.contains("ada") != true { return 3; }
	if resp.body.contains("7") != true { return 4; }

	println("auto-json HTTP response test passed!");
	return 0;
}
