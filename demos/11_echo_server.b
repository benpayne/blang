// Demo 11: Echo Server with Selector
//
// A simple TCP echo server using the BLang networking library.
// Listens on port 8080, echoes any received data back to the client.
// Uses the poll-based Selector for non-blocking event-driven I/O.
//
// Run: bcc demos/11_echo_server.b -o echo_server && ./echo_server
// Test: nc localhost 8080   (type a message, see it echoed back)

import net;

fn main() -> int {
	int port = 9898;
	net.ServerSocket server = net.server_bind("0.0.0.0", port);
	net.Selector sel = net.selector_create();

	println("Echo server listening on port {}...", port);

	sel.on_accept(server, fn(net.Socket conn) {
		println("Client connected");
		sel.on_data(conn, fn(net.Socket s) {
			string data = s.read(4096);
			println("Received data: {}", data);
			if data.is_empty() {
				println("Client disconnected");
				s.close();
			} else {
				data = data + " (echoed)";
				println("Echoing data: {}", data);
				s.write(data);
			}
		});
	});

	sel.join();
	server.close();
	return 0;
}
