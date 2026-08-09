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
extern fn __blang_tcp_read_into_byte_array(int fd, Array<byte> arr, long max_len) -> long;
extern fn __blang_tcp_write_byte_array(int fd, Array<byte> arr) -> long;
extern fn __blang_tcp_write_all(int fd, string data) -> long;
extern fn __blang_sendfile(int sock_fd, int file_fd, long offset, long count) -> long;

extern fn __blang_selector_create() -> int;
extern fn __blang_selector_add_read(int sel, int fd, fn(int) handler);
extern fn __blang_selector_add_accept(int sel, int listen_fd, fn(int) handler);
extern fn __blang_selector_remove(int sel, int fd);
extern fn __blang_selector_run(int sel);
extern fn __blang_selector_wait(int sel);
extern fn __blang_selector_shutdown(int sel);
extern fn __blang_selector_destroy(int sel);

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
	pub fn read(self, int max_len) -> string {
		return __blang_tcp_read(self.fd, max_len);
	}

	pub fn write(self, string data) -> int {
		return __blang_tcp_write_string(self.fd, data);
	}

	pub fn close(self) {
		__blang_tcp_close(self.fd);
	}
}

impl Socket {
	fn read_into(self, Buffer buf, int max_len) -> int {
		long max_long = max_len;
		long n = __blang_tcp_read_into_byte_array(self.fd, buf.get_bytes(), max_long);
		int result = n;
		return result;
	}

	fn write_buffer(self, Buffer buf) -> int {
		long n = __blang_tcp_write_byte_array(self.fd, buf.get_bytes());
		int result = n;
		return result;
	}

	// Write all bytes, retrying on partial writes.
	fn write_all(self, string data) -> int {
		long n = __blang_tcp_write_all(self.fd, data);
		int result = n;
		return result;
	}

	// Stream a file directly to this socket.
	// Reads from file_fd at the given offset, sends count bytes.
	// Uses 8KB kernel-space chunks — no BLang-level buffering needed.
	fn sendfile(self, int file_fd, long offset, long count) -> long {
		return __blang_sendfile(self.fd, file_fd, offset, count);
	}

	pub fn get_fd(self) -> int {
		return self.fd;
	}
}

// --- ServerSocket ---
pub struct ServerSocket {
	int fd;
}

impl FileOps for ServerSocket {
	pub fn read(self, int max_len) -> string {
		return __blang_tcp_read(self.fd, max_len);
	}

	pub fn write(self, string data) -> int {
		return __blang_tcp_write_string(self.fd, data);
	}

	pub fn close(self) {
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

// Wrap a raw connection fd in a Socket. The one external construction form for
// a Socket once its `fd` field goes opaque (modules-v2-exports): a consumer that
// has an fd (e.g. from an accept callback) builds a Socket without the literal.
pub fn socket_from_fd(int fd) -> Socket {
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
	// The event loop runs on a dedicated spawned thread. Capture only the int
	// handle (not the Selector struct) so nothing crosses the spawn boundary that
	// would require shared/sync (U7 concurrency safety).
	spawn { __blang_selector_run(h); }
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

impl HttpRequest {
	pub fn method(self) -> string { return self.method; }
	pub fn path(self) -> string { return self.path; }
	pub fn body(self) -> string { return self.body; }
}

// --- HttpResponse ---
pub struct HttpResponse {
	int status;
	string content_type;
	string body;
}

impl HttpResponse {
	pub fn status(self) -> int { return self.status; }
	pub fn content_type(self) -> string { return self.content_type; }
	pub fn body(self) -> string { return self.body; }
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

// A single method+path route with its handler. HttpServer keeps a table of
// these; dispatch_request matches a request against them. (Ported from origin.)
pub struct Route {
	string method;
	string path;
	fn(HttpRequest) -> HttpResponse handler;
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

// --- HttpServer struct (impl block is below, after HTTP utility functions) ---
pub struct HttpServer {
	int _selector_handle;
	int _server_fd;
	Array<Route> _routes;
}

// ================================================================
// BLang-native HTTP utilities
// ================================================================
//
// These functions implement HTTP parsing and response building in
// pure BLang, using Buffer for byte-level manipulation and string
// operations for response construction.

// --- Hex conversion for chunked encoding ---
// Converts a non-negative integer to a lowercase hex string.
pub fn int_to_hex(int value) -> string {
	if value == 0 { return "0"; }
	string digits = "0123456789abcdef";
	string result = "";
	int v = value;
	for {
		if v <= 0 { break; }
		int rem = v % 16;
		string d = digits.substring(rem, rem + 1);
		result = d + result;
		v = v / 16;
	}
	return result;
}

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

// --- Streaming HTTP response helpers ---

// Send HTTP response headers on a socket (status line + custom headers + blank line).
// Does NOT send a body. The caller can stream body data after this call.
pub fn send_http_headers(Socket conn, int status, string headers) -> int {
	string reason = http_status_text(status);
	string head = "HTTP/1.1 {status} {reason}\r\n{headers}\r\n";
	return conn.write_all(head);
}

// Send a single chunk in HTTP chunked transfer encoding on a socket.
pub fn send_http_chunk(Socket conn, string data) -> int {
	int len = data.length;
	string hex_len = int_to_hex(len);
	string chunk = hex_len + "\r\n" + data + "\r\n";
	return conn.write_all(chunk);
}

// Send the final zero-length chunk to end chunked transfer encoding.
pub fn end_http_chunked(Socket conn) -> int {
	return conn.write_all("0\r\n\r\n");
}

// --- HTTP request parsing (pure BLang, using Buffer) ---

// Parsed HTTP request line components.
pub struct HttpRequestLine {
	string method;
	string path;
	string version;
}

impl HttpRequestLine {
	pub fn method(self) -> string { return self.method; }
	pub fn path(self) -> string { return self.path; }
	pub fn version(self) -> string { return self.version; }
}

// Parse the HTTP request line from a Buffer.
pub fn parse_http_request_line(Buffer buf) -> HttpRequestLine {
	Buffer crlf = Buffer(2);
	crlf.append_byte(13);
	crlf.append_byte(10);
	int line_end = buf.index_of(crlf, 0);
	if line_end < 0 {
		return HttpRequestLine { method: "", path: "", version: "" };
	}

	string line = buf.to_string_range(0, line_end);

	Buffer space = Buffer(1);
	space.append_byte(32);

	Buffer line_buf = Buffer.from_string(line);
	int sp1 = line_buf.index_of(space, 0);
	if sp1 < 0 {
		return HttpRequestLine { method: "", path: "", version: "" };
	}

	string method = line_buf.to_string_range(0, sp1);

	int sp2 = line_buf.index_of(space, sp1 + 1);
	if sp2 < 0 {
		return HttpRequestLine { method: "", path: "", version: "" };
	}

	string path = line_buf.to_string_range(sp1 + 1, sp2);
	string version = line_buf.to_string_range(sp2 + 1, line_end);

	return HttpRequestLine { method: method, path: path, version: version };
}

// Parse HTTP headers from a Buffer into parallel arrays of key-value strings.
pub struct HttpParsedHeaders {
	Array<string> keys;
	Array<string> values;
}

// Designed lookup surface: the parallel-array representation is deliberately not
// exposed (it is the representation most likely to change). `has`/`get` are the
// lookup operations consumers want; `count`/`key`/`value` retain ordered
// iteration without handing out the arrays.
impl HttpParsedHeaders {
	pub fn count(self) -> int { return self.keys.length; }

	pub fn key(self, int i) -> string { return self.keys[i]; }

	pub fn value(self, int i) -> string { return self.values[i]; }

	pub fn has(self, string name) -> bool {
		for i in 0..self.keys.length {
			if self.keys[i] == name { return true; }
		}
		return false;
	}

	pub fn get(self, string name) -> string {
		for i in 0..self.keys.length {
			if self.keys[i] == name { return self.values[i]; }
		}
		return "";
	}
}

pub fn parse_http_headers_from_buffer(Buffer buf) -> HttpParsedHeaders {
	Array<string> keys = [];
	Array<string> values = [];

	Buffer crlf2 = Buffer(4);
	crlf2.append_byte(13);
	crlf2.append_byte(10);
	crlf2.append_byte(13);
	crlf2.append_byte(10);
	int header_end = buf.index_of(crlf2, 0);
	if header_end < 0 {
		return HttpParsedHeaders { keys: keys, values: values };
	}

	Buffer crlf = Buffer(2);
	crlf.append_byte(13);
	crlf.append_byte(10);
	int first_line_end = buf.index_of(crlf, 0);
	if first_line_end < 0 {
		return HttpParsedHeaders { keys: keys, values: values };
	}

	int pos = first_line_end + 2;

	Buffer colon = Buffer(1);
	colon.append_byte(58);

	for {
		if pos >= header_end {
			break;
		}
		int line_end = buf.index_of(crlf, pos);
		if line_end < 0 {
			break;
		}
		if line_end == pos {
			break;
		}

		Buffer line_buf = buf.slice(pos, line_end);
		int colon_pos = line_buf.index_of(colon, 0);
		if colon_pos >= 0 {
			string key = line_buf.to_string_range(0, colon_pos);

			int val_start = colon_pos + 1;
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
pub fn extract_http_body(Buffer buf) -> string {
	Buffer sep = Buffer(4);
	sep.append_byte(13);
	sep.append_byte(10);
	sep.append_byte(13);
	sep.append_byte(10);
	int header_end = buf.index_of(sep, 0);
	if header_end < 0 {
		return "";
	}
	int body_start = header_end + 4;
	int buf_len = buf.get_length();
	if body_start >= buf_len {
		return "";
	}
	return buf.to_string_range(body_start, buf_len);
}

// --- BLang-native HTTP GET client (using Buffer) ---
pub fn http_get_buffered(string host, int port, string path) -> string {
	Socket sock = tcp_connect(host, port);

	string request = "GET {path} HTTP/1.1\r\nHost: {host}\r\nConnection: close\r\n\r\n";
	sock.write(request);

	Buffer buf = Buffer(4096);
	for {
		int n = sock.read_into(buf, 8192);
		if n <= 0 {
			break;
		}
	}
	sock.close();

	return extract_http_body(buf);
}

// --- HTTP Client (public API) ---
pub fn http_get(string host, int port, string path) -> string {
	return http_get_buffered(host, port, path);
}

// ================================================================
// HttpServer implementation (pure BLang, uses Selector + HTTP parsing)
// ================================================================

impl HttpServer {
	// --- Route registration (ported from origin). get()/post()/put() are sugar
	// over route(); `delete` is a reserved keyword, so DELETE routes use
	// route("DELETE", ...). Handlers are stored in _routes and matched by serve().
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

	// Serve registered routes: parse each accepted connection into an
	// HttpRequest, dispatch to the matching route (or 404), write the response.
	// Modeled on on_request but dispatching through the route table; uses local's
	// byte-array TCP helpers (origin's version used the Buffer-based helpers).
	fn serve(self) {
		Array<Route> routes = self._routes;
		int sel = self._selector_handle;
		int sfd = self._server_fd;
		__blang_selector_add_accept(sel, sfd, fn(int new_fd) {
			Buffer buf = Buffer(4096);

			Buffer sep = Buffer(4);
			sep.append_byte(13);
			sep.append_byte(10);
			sep.append_byte(13);
			sep.append_byte(10);

			long max_read = 4096;
			for {
				long n = __blang_tcp_read_into_byte_array(new_fd, buf.get_bytes(), max_read);
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
			Buffer buf = Buffer(4096);

			Buffer sep = Buffer(4);
			sep.append_byte(13);
			sep.append_byte(10);
			sep.append_byte(13);
			sep.append_byte(10);

			long max_read = 4096;
			for {
				long n = __blang_tcp_read_into_byte_array(new_fd, buf.get_bytes(), max_read);
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

			HttpResponse resp = handler(req);

			string response_str = build_http_response(resp.status, resp.content_type, resp.body);
			__blang_tcp_write_string(new_fd, response_str);
			__blang_tcp_close(new_fd);
		});
	}

	// Streaming handler: callback receives HttpRequest + Socket.
	// The handler is responsible for sending response headers and body
	// directly through the socket, then the connection is closed.
	fn on_stream_request(self, fn(HttpRequest, Socket) handler) {
		int sel = self._selector_handle;
		int sfd = self._server_fd;
		__blang_selector_add_accept(sel, sfd, fn(int new_fd) {
			Buffer buf = Buffer(4096);

			Buffer sep = Buffer(4);
			sep.append_byte(13);
			sep.append_byte(10);
			sep.append_byte(13);
			sep.append_byte(10);

			long max_read = 4096;
			for {
				long n = __blang_tcp_read_into_byte_array(new_fd, buf.get_bytes(), max_read);
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

			Socket conn = Socket { fd: new_fd };
			handler(req, conn);
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
	spawn { __blang_selector_run(sel); }
	Array<Route> routes = [];
	return HttpServer { _selector_handle: sel, _server_fd: server_fd, _routes: routes };
}
