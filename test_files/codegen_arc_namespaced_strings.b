// ARC ledger #5 (closed): a NAMESPACED stdlib module whose functions call each
// other returning strings (net.build_http_response -> net.http_status_text
// internally, under net__ module-prefix codegen). Previously this shape
// double-freed — the reason `cli`/`collections` were parsed into the global
// scope as a workaround. Deterministic (no sockets): only the pure string
// helpers are exercised.

import net;

fn main() -> int {
	// Internal chain: build_http_response calls http_status_text, builds an
	// interpolated response, and returns it across the namespace boundary.
	string resp = net.build_http_response(200, "text/plain", "hello");
	println("{}", resp.contains("200 OK"));
	println("{}", resp.contains("Content-Length: 5"));
	println("{}", resp.ends_with("hello"));

	// Repeatedly, so per-call temps/returns balance across iterations.
	int total = 0;
	for i in 0..20 {
		string r = net.build_http_response(404, "text/html", "missing {i}");
		total = total + r.length;
	}
	println("{}", total > 0);

	// Direct call to the inner helper too.
	println("{}", net.http_status_text(500));
	return 0;
}
