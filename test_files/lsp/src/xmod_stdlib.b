// modules-v2-lsp U4 fixture: cross-module navigation into the stdlib — a stdlib
// function (`net.http_ok`) and a stdlib type named in type position
// (`net.HttpServer`). Compiles clean through bcc; never run (it would listen).
import net;

fn respond() -> int {
	int code = 200;
	net.HttpResponse r = net.http_ok("hi");
	net.HttpServer server = net.http_server("0.0.0.0", 8080);
	return code;
}

fn main() -> int {
	return 0;
}
