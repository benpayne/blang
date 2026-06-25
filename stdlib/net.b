// stdlib/net.b — BLang networking standard library
//
// Usage: import net;
//   net.server_bind(host, port)   -> ServerSocket
//   net.tcp_connect(host, port)   -> Socket
//   net.selector_create()         -> Selector
//   net.http_server(host, port)   -> HttpServer
//   net.http_get(host, port, path) -> string
//   net.http_ok(body)             -> HttpResponse
//   net.http_json(body)           -> HttpResponse
//   net.http_not_found()          -> HttpResponse
//   net.http_response(status, ct, body) -> HttpResponse
//
// Provides Socket, ServerSocket, Selector, HttpServer, HttpRequest,
// and HttpResponse types wrapping the C runtime.

// C runtime declarations (internal — users should not call these directly)
extern fn __blang_tcp_listen(cstring host, int port, int backlog) -> int;
extern fn __blang_tcp_accept(int listen_fd) -> int;
extern fn __blang_tcp_close(int fd);
extern fn __blang_tcp_connect(cstring host, int port) -> int;
extern fn __blang_tcp_read(int fd, int max_len) -> string;
extern fn __blang_tcp_write_string(int fd, string data) -> int;
extern fn __blang_tcp_read_into_buffer(int fd, Buffer buf, int max_len) -> int;
extern fn __blang_tcp_write_buffer(int fd, Buffer buf) -> int;

extern fn __blang_selector_create() -> int;
extern fn __blang_selector_add_read(int sel, int fd, fn(int) handler);
extern fn __blang_selector_add_accept(int sel, int listen_fd, fn(int) handler);
extern fn __blang_selector_remove(int sel, int fd);
extern fn __blang_selector_run(int sel);
extern fn __blang_selector_wait(int sel);
extern fn __blang_selector_shutdown(int sel);
extern fn __blang_selector_destroy(int sel);

// Buffer creation externs (Buffer is a builtin type with codegen methods)
extern fn __blang_buffer_create(long capacity) -> Buffer;
extern fn __blang_buffer_create_from_string(string s) -> Buffer;

// --- FileOps protocol (shared by Socket and ServerSocket) ---
pub protocol FileOps {
	fn read(self, int max_len) -> string;
	fn write(self, string data) -> int;
	fn close(self);
}

// --- Socket ---
pub struct Socket {
	int fd;
}

impl FileOps for Socket {
	fn read(self, int max_len) -> string {
		return __blang_tcp_read(self.fd, max_len);
	}

	fn write(self, string data) -> int {
		return __blang_tcp_write_string(self.fd, data);
	}

	fn close(self) {
		__blang_tcp_close(self.fd);
	}
}

impl Socket {
	fn read_into(self, Buffer buf, int max_len) -> int {
		return __blang_tcp_read_into_buffer(self.fd, buf, max_len);
	}

	fn write_buffer(self, Buffer buf) -> int {
		return __blang_tcp_write_buffer(self.fd, buf);
	}
}

// --- ServerSocket ---
pub struct ServerSocket {
	int fd;
}

impl FileOps for ServerSocket {
	fn read(self, int max_len) -> string {
		return __blang_tcp_read(self.fd, max_len);
	}

	fn write(self, string data) -> int {
		return __blang_tcp_write_string(self.fd, data);
	}

	fn close(self) {
		__blang_tcp_close(self.fd);
	}
}

impl ServerSocket {
	fn accept(self) -> Socket {
		int conn_fd = __blang_tcp_accept(self.fd);
		return Socket { fd: conn_fd };
	}
}

// --- Free functions for creating sockets ---

pub fn server_bind(string host, int port) -> ServerSocket {
	int fd = __blang_tcp_listen(host, port, 10);
	return ServerSocket { fd: fd };
}

pub fn tcp_connect(string host, int port) -> Socket {
	int fd = __blang_tcp_connect(host, port);
	return Socket { fd: fd };
}

// --- Selector ---
// Event-driven I/O multiplexer. Owns a dedicated thread (via spawn)
// that runs the poll-based event loop. Register callbacks with
// on_accept/on_data, then call wait() to block until shutdown.
pub struct Selector {
	int handle;
}

impl Selector {
	// Register a callback for new connections on a server socket.
	// Callback receives a Socket wrapping the accepted connection fd.
	fn on_accept(self, ServerSocket server, fn(Socket) callback) {
		__blang_selector_add_accept(self.handle, server.fd, fn(int fd) {
			callback(Socket { fd: fd });
		});
	}

	// Register a callback for data ready on a connection.
	// Callback receives the Socket so the handler can read/write/close.
	fn on_data(self, Socket conn, fn(Socket) callback) {
		__blang_selector_add_read(self.handle, conn.fd, fn(int fd) {
			callback(Socket { fd: fd });
		});
	}

	fn remove(self, Socket conn) {
		__blang_selector_remove(self.handle, conn.fd);
	}

	// Block the calling thread until the selector is shut down.
	// The event loop runs on its own spawned thread; this just waits.
	fn join(self) {
		__blang_selector_wait(self.handle);
	}

	fn shutdown(self) {
		__blang_selector_shutdown(self.handle);
	}
}

pub fn selector_create() -> Selector {
	int h = __blang_selector_create();
	Selector sel = Selector { handle: h };
	// The event loop runs on a dedicated spawned thread.
	spawn { __blang_selector_run(sel.handle); }
	return sel;
}

// ================================================================
// HTTP support (pure BLang — no C HTTP runtime needed)
// ================================================================

// --- HttpRequest ---
pub struct HttpRequest {
	string method;
	string path;
	string body;
}

// --- HttpResponse ---
pub struct HttpResponse {
	int status;
	string content_type;
	string body;
}

// Convenience constructors
pub fn http_response(int status, string content_type, string body) -> HttpResponse {
	return HttpResponse { status: status, content_type: content_type, body: body };
}

pub fn http_ok(string body) -> HttpResponse {
	return HttpResponse { status: 200, content_type: "text/plain", body: body };
}

pub fn http_json(string body) -> HttpResponse {
	return HttpResponse { status: 200, content_type: "application/json", body: body };
}

pub fn http_not_found() -> HttpResponse {
	return HttpResponse { status: 404, content_type: "text/plain", body: "Not Found" };
}

// --- Route: a (method, path) pair bound to a handler ---
pub struct Route {
	string method;
	string path;
	fn(HttpRequest) -> HttpResponse handler;
}

// --- HttpServer struct (impl block is below, after HTTP utility functions) ---
pub struct HttpServer {
	int _selector_handle;
	int _server_fd;
	Array<Route> _routes;
}

// Find the route matching a request and invoke its handler, returning the
// handler's response; falls back to 404 when nothing matches. Pure function —
// testable without a live socket.
pub fn dispatch_request(Array<Route> routes, HttpRequest req) -> HttpResponse {
	int i = 0;
	for i in 0..routes.length {
		Route r = routes[i];
		if r.method == req.method {
			if r.path == req.path {
				fn(HttpRequest) -> HttpResponse h = r.handler;
				return h(req);
			}
		}
	}
	return http_not_found();
}

// ================================================================
// BLang-native HTTP utilities
// ================================================================
//
// These functions implement HTTP parsing and response building in
// pure BLang, using Buffer for byte-level manipulation and string
// operations for response construction.

// --- HTTP status text ---
// Returns the standard HTTP reason phrase for a status code.
pub fn http_status_text(int code) -> string {
	if code == 200 { return "OK"; }
	if code == 201 { return "Created"; }
	if code == 204 { return "No Content"; }
	if code == 301 { return "Moved Permanently"; }
	if code == 302 { return "Found"; }
	if code == 304 { return "Not Modified"; }
	if code == 400 { return "Bad Request"; }
	if code == 401 { return "Unauthorized"; }
	if code == 403 { return "Forbidden"; }
	if code == 404 { return "Not Found"; }
	if code == 405 { return "Method Not Allowed"; }
	if code == 500 { return "Internal Server Error"; }
	if code == 502 { return "Bad Gateway"; }
	if code == 503 { return "Service Unavailable"; }
	return "Unknown";
}

// --- HTTP response building (pure BLang) ---
// Builds a complete HTTP/1.1 response string with headers and body.
pub fn build_http_response(int status, string content_type, string body) -> string {
	string reason = http_status_text(status);
	int body_len = body.length;
	string response = "HTTP/1.1 {status} {reason}\r\nContent-Type: {content_type}\r\nContent-Length: {body_len}\r\nConnection: close\r\n\r\n" + body;
	return response;
}

// --- HTTP request parsing (pure BLang, using Buffer) ---

// Parsed HTTP request line components.
pub struct HttpRequestLine {
	string method;
	string path;
	string version;
}

// Parse the HTTP request line from a Buffer.
// Expects the buffer to start with "METHOD PATH VERSION\r\n...".
// Returns an HttpRequestLine with method, path, and version extracted.
// If parsing fails, returns empty strings.
pub fn parse_http_request_line(Buffer buf) -> HttpRequestLine {
	// Find the first line ending (\r\n)
	Buffer crlf = __blang_buffer_create(2);
	crlf.append_byte(13);
	crlf.append_byte(10);
	int line_end = buf.index_of(crlf, 0);
	if line_end < 0 {
		return HttpRequestLine { method: "", path: "", version: "" };
	}

	// Extract the request line as a string
	string line = buf.to_string_range(0, line_end);

	// Find first space (between method and path)
	Buffer space = __blang_buffer_create(1);
	space.append_byte(32);

	Buffer line_buf = __blang_buffer_create_from_string(line);
	int sp1 = line_buf.index_of(space, 0);
	if sp1 < 0 {
		return HttpRequestLine { method: "", path: "", version: "" };
	}

	string method = line_buf.to_string_range(0, sp1);

	// Find second space (between path and version)
	int sp2 = line_buf.index_of(space, sp1 + 1);
	if sp2 < 0 {
		return HttpRequestLine { method: "", path: "", version: "" };
	}

	string path = line_buf.to_string_range(sp1 + 1, sp2);
	string version = line_buf.to_string_range(sp2 + 1, line_end);

	return HttpRequestLine { method: method, path: path, version: version };
}

// Parse HTTP headers from a Buffer into parallel arrays of key-value strings.
// The buffer should contain the full HTTP message (request line + headers + body).
// Returns an HttpParsedHeaders struct with arrays of keys and values.
pub struct HttpParsedHeaders {
	Array<string> keys;
	Array<string> values;
}

pub fn parse_http_headers_from_buffer(Buffer buf) -> HttpParsedHeaders {
	Array<string> keys = [];
	Array<string> values = [];

	// Find \r\n\r\n (header terminator)
	Buffer crlf2 = __blang_buffer_create(4);
	crlf2.append_byte(13);
	crlf2.append_byte(10);
	crlf2.append_byte(13);
	crlf2.append_byte(10);
	int header_end = buf.index_of(crlf2, 0);
	if header_end < 0 {
		return HttpParsedHeaders { keys: keys, values: values };
	}

	// Find end of first line (request line) — skip it
	Buffer crlf = __blang_buffer_create(2);
	crlf.append_byte(13);
	crlf.append_byte(10);
	int first_line_end = buf.index_of(crlf, 0);
	if first_line_end < 0 {
		return HttpParsedHeaders { keys: keys, values: values };
	}

	// Parse each header line from after the request line to before the blank line
	int pos = first_line_end + 2;

	Buffer colon = __blang_buffer_create(1);
	colon.append_byte(58);

	for {
		if pos >= header_end {
			break;
		}
		// Find end of this header line
		int line_end = buf.index_of(crlf, pos);
		if line_end < 0 {
			break;
		}
		if line_end == pos {
			break;
		}

		// Find colon within this line
		// Extract the line as a sub-buffer to search within it
		Buffer line_buf = buf.slice(pos, line_end);
		int colon_pos = line_buf.index_of(colon, 0);
		if colon_pos >= 0 {
			string key = line_buf.to_string_range(0, colon_pos);

			// Skip ": " (colon + optional space)
			int val_start = colon_pos + 1;
			// Skip leading spaces in value
			for {
				if val_start >= line_end - pos {
					break;
				}
				if line_buf.get(val_start) != 32 {
					break;
				}
				val_start = val_start + 1;
			}
			string value = line_buf.to_string_range(val_start, line_end - pos);
			keys.push(key);
			values.push(value);
		}

		pos = line_end + 2;
	}

	return HttpParsedHeaders { keys: keys, values: values };
}

// Extract the body from an HTTP message in a Buffer.
// Returns the portion after the \r\n\r\n header separator.
pub fn extract_http_body(Buffer buf) -> string {
	Buffer sep = __blang_buffer_create(4);
	sep.append_byte(13);
	sep.append_byte(10);
	sep.append_byte(13);
	sep.append_byte(10);
	int header_end = buf.index_of(sep, 0);
	if header_end < 0 {
		return "";
	}
	int body_start = header_end + 4;
	if body_start >= buf.length {
		return "";
	}
	return buf.to_string_range(body_start, buf.length);
}

// --- BLang-native HTTP GET client (using Buffer) ---
// Performs a blocking HTTP GET request and returns the response body.
pub fn http_get_buffered(string host, int port, string path) -> string {
	Socket sock = tcp_connect(host, port);

	// Build GET request using string interpolation
	string request = "GET {path} HTTP/1.1\r\nHost: {host}\r\nConnection: close\r\n\r\n";
	sock.write(request);

	// Read response into buffer
	Buffer buf = __blang_buffer_create(4096);
	for {
		int n = sock.read_into(buf, 8192);
		if n <= 0 {
			break;
		}
	}
	sock.close();

	// Extract body (after \r\n\r\n)
	return extract_http_body(buf);
}

// --- HTTP Client (public API) ---
// Uses BLang-native implementation with Buffer I/O (no C runtime needed)
pub fn http_get(string host, int port, string path) -> string {
	return http_get_buffered(host, port, path);
}

// Performs a blocking HTTP POST with a request body and returns the response
// body. content_type sets the Content-Type header (e.g. "application/json").
pub fn http_post(string host, int port, string path, string content_type, string body) -> string {
	Socket sock = tcp_connect(host, port);

	int body_len = body.length;
	string request = "POST {path} HTTP/1.1\r\nHost: {host}\r\nContent-Type: {content_type}\r\nContent-Length: {body_len}\r\nConnection: close\r\n\r\n" + body;
	sock.write(request);

	Buffer buf = __blang_buffer_create(4096);
	for {
		int n = sock.read_into(buf, 8192);
		if n <= 0 {
			break;
		}
	}
	sock.close();

	return extract_http_body(buf);
}

// ================================================================
// HttpServer implementation (pure BLang, uses Selector + HTTP parsing)
// ================================================================

impl HttpServer {
	// --- Route registration ---
	// Register a handler for a given method + path. get()/post() are sugar.
	fn route(self, string method, string path, fn(HttpRequest) -> HttpResponse handler) {
		self._routes.push(Route { method: method, path: path, handler: handler });
	}

	fn get(self, string path, fn(HttpRequest) -> HttpResponse handler) {
		self._routes.push(Route { method: "GET", path: path, handler: handler });
	}

	fn post(self, string path, fn(HttpRequest) -> HttpResponse handler) {
		self._routes.push(Route { method: "POST", path: path, handler: handler });
	}

	fn put(self, string path, fn(HttpRequest) -> HttpResponse handler) {
		self._routes.push(Route { method: "PUT", path: path, handler: handler });
	}

	// Note: there is no dedicated delete() method because `delete` is a reserved
	// keyword. Use route("DELETE", path, handler) for DELETE routes.

	// Start serving registered routes. Each accepted connection is parsed into
	// an HttpRequest, dispatched to the matching route (or 404), and the
	// response is written back. Call after registering routes; then join().
	fn serve(self) {
		int sel = self._selector_handle;
		int sfd = self._server_fd;
		Array<Route> routes = self._routes;
		__blang_selector_add_accept(sel, sfd, fn(int new_fd) {
			Buffer buf = __blang_buffer_create(4096);
			Buffer sep = __blang_buffer_create(4);
			sep.append_byte(13);
			sep.append_byte(10);
			sep.append_byte(13);
			sep.append_byte(10);
			for {
				int n = __blang_tcp_read_into_buffer(new_fd, buf, 4096);
				if n <= 0 { break; }
				if buf.index_of(sep, 0) >= 0 { break; }
			}

			HttpRequestLine rline = parse_http_request_line(buf);
			string body = extract_http_body(buf);
			HttpRequest req = HttpRequest {
				method: rline.method,
				path: rline.path,
				body: body
			};

			HttpResponse resp = dispatch_request(routes, req);
			string response_str = build_http_response(resp.status, resp.content_type, resp.body);
			__blang_tcp_write_string(new_fd, response_str);
			__blang_tcp_close(new_fd);
		});
	}

	fn on_request(self, fn(HttpRequest) -> HttpResponse handler) {
		int sel = self._selector_handle;
		int sfd = self._server_fd;
		__blang_selector_add_accept(sel, sfd, fn(int new_fd) {
			// Read HTTP request (blocking) into buffer
			Buffer buf = __blang_buffer_create(4096);

			// Build \r\n\r\n separator for header detection
			Buffer sep = __blang_buffer_create(4);
			sep.append_byte(13);
			sep.append_byte(10);
			sep.append_byte(13);
			sep.append_byte(10);

			// Read until complete headers or EOF
			for {
				int n = __blang_tcp_read_into_buffer(new_fd, buf, 4096);
				if n <= 0 { break; }
				if buf.index_of(sep, 0) >= 0 { break; }
			}

			// Parse request line and body
			HttpRequestLine rline = parse_http_request_line(buf);
			string body = extract_http_body(buf);

			HttpRequest req = HttpRequest {
				method: rline.method,
				path: rline.path,
				body: body
			};

			// Dispatch to user handler
			HttpResponse resp = handler(req);

			// Build and send HTTP response
			string response_str = build_http_response(resp.status, resp.content_type, resp.body);
			__blang_tcp_write_string(new_fd, response_str);
			__blang_tcp_close(new_fd);
		});
	}

	fn join(self) {
		__blang_selector_wait(self._selector_handle);
	}

	fn shutdown(self) {
		__blang_selector_shutdown(self._selector_handle);
	}
}

pub fn http_server(string host, int port) -> HttpServer {
	int server_fd = __blang_tcp_listen(host, port, 10);
	int sel = __blang_selector_create();
	// Spawn the event loop on a dedicated thread.
	spawn { __blang_selector_run(sel); }
	Array<Route> routes = [];
	return HttpServer { _selector_handle: sel, _server_fd: server_fd, _routes: routes };
}
