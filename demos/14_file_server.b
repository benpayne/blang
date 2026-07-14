// Demo 14: HTTP File Server
//
// A static file server with directory listing, content-type detection,
// path traversal protection, HEAD support, and streaming for large files.
//
// Run: bcc demos/14_file_server.b -o file_server && ./file_server [port] [doc_root]
// Test: curl http://127.0.0.1:8080/
//       curl http://127.0.0.1:8080/test.txt
//       curl -I http://127.0.0.1:8080/test.txt    (HEAD request)

import net;
import fs;

fn get_content_type(string path) -> string {
	if path.ends_with(".html") { return "text/html"; }
	if path.ends_with(".htm") { return "text/html"; }
	if path.ends_with(".css") { return "text/css"; }
	if path.ends_with(".js") { return "application/javascript"; }
	if path.ends_with(".json") { return "application/json"; }
	if path.ends_with(".png") { return "image/png"; }
	if path.ends_with(".jpg") { return "image/jpeg"; }
	if path.ends_with(".jpeg") { return "image/jpeg"; }
	if path.ends_with(".gif") { return "image/gif"; }
	if path.ends_with(".svg") { return "image/svg+xml"; }
	if path.ends_with(".txt") { return "text/plain"; }
	if path.ends_with(".xml") { return "text/xml"; }
	if path.ends_with(".ico") { return "image/x-icon"; }
	if path.ends_with(".wasm") { return "application/wasm"; }
	if path.ends_with(".pdf") { return "application/pdf"; }
	if path.ends_with(".zip") { return "application/zip"; }
	if path.ends_with(".mp4") { return "video/mp4"; }
	if path.ends_with(".mp3") { return "audio/mpeg"; }
	return "application/octet-stream";
}

fn build_index_page(string dir_path, string url_path) -> string {
	Array<string> entries = fs.list_dir(dir_path);
	string html = "<html><head><title>Index of " + url_path + "</title></head>\n";
	html = html + "<body><h1>Index of " + url_path + "</h1><hr><ul>\n";

	// Parent directory link (unless at root)
	if url_path != "/" {
		html = html + "<li><a href=\"..\">..</a></li>\n";
	}

	for item in entries {
		string entry_path = dir_path + "/" + item;
		string link = "<li><a href=\"" + item + "\">" + item + "</a></li>\n";
		if fs.is_dir(entry_path) {
			link = "<li><a href=\"" + item + "/\">" + item + "/</a></li>\n";
		}
		html = html + link;
	}

	html = html + "</ul><hr></body></html>\n";
	return html;
}

// Send an error response (small body, no streaming needed).
fn send_error(net.Socket conn, int status, string message, bool is_head) {
	long body_len = message.length;
	string headers = "Content-Type: text/plain\r\nContent-Length: {body_len}\r\nConnection: close";
	net.send_http_headers(conn, status, headers);
	if is_head == false {
		conn.write_all(message);
	}
}

// Send a small response with full body in memory (directory listings, index.html).
fn send_response(net.Socket conn, int status, string content_type, string body, bool is_head) {
	long body_len = body.length;
	string headers = "Content-Type: {content_type}\r\nContent-Length: {body_len}\r\nConnection: close";
	net.send_http_headers(conn, status, headers);
	if is_head == false {
		conn.write_all(body);
	}
}

// Stream a file to the connection.
// Sends headers with Content-Length, then streams the file in 8KB kernel-space
// chunks using sendfile — the file is never fully loaded into memory.
fn stream_file(net.Socket conn, string path, string content_type, bool is_head) {
	long file_size = fs.file_size(path);
	string headers = "Content-Type: {content_type}\r\nContent-Length: {file_size}\r\nConnection: close";
	net.send_http_headers(conn, 200, headers);

	if is_head { return; }

	// Stream the file through the socket
	fs.File f = fs.open(path, "r");
	int file_fd = f.get_fd();
	long zero = 0;
	conn.sendfile(file_fd, zero, file_size);
	f.close();
}

fn handle_request(net.HttpRequest req, net.Socket conn, string doc_root) {
	println("{} {}", req.method, req.path);

	bool is_head = req.method == "HEAD";

	// Only allow GET and HEAD requests
	if req.method != "GET" {
		if is_head == false {
			send_error(conn, 405, "Method Not Allowed", false);
			return;
		}
	}

	// Reject path traversal attempts
	if req.path.contains("..") {
		send_error(conn, 403, "Forbidden", is_head);
		return;
	}

	// Build the filesystem path
	string request_path = req.path;
	string full_path = doc_root + request_path;

	// Check if path exists
	if fs.exists(full_path) == false {
		send_error(conn, 404, "Not Found", is_head);
		return;
	}

	// If it's a directory, try index.html or generate listing
	if fs.is_dir(full_path) {
		string index_path = full_path + "/index.html";
		if fs.exists(index_path) {
			// Index files are typically small — send in memory
			string content = fs.read_all(index_path);
			send_response(conn, 200, "text/html", content, is_head);
			return;
		}
		// Generate directory listing
		string listing = build_index_page(full_path, request_path);
		send_response(conn, 200, "text/html", listing, is_head);
		return;
	}

	// Serve the file — stream it to avoid loading into memory
	string ctype = get_content_type(full_path);
	stream_file(conn, full_path, ctype, is_head);
}

fn main() -> int {
	// Default port and document root
	int port = 8080;
	string doc_root = ".";

	// Command-line args available via sys.args but string-to-int
	// conversion not yet supported, so defaults are used

	println("File server listening on port {}...", port);
	println("Serving files from: {}", doc_root);

	net.HttpServer server = net.http_server("0.0.0.0", port);
	server.on_stream_request(fn(net.HttpRequest req, net.Socket conn) {
		handle_request(req, conn, doc_root);
	});

	server.join();
	return 0;
}
