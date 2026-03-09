// E2E test: TCP echo server — listen, accept, read, write back, close.
// Uses spawn for client so server and client run concurrently.
// All I/O goes through stdlib Socket/ServerSocket methods.

import net;

fn main() -> int {
	net.ServerSocket server = net.server_bind("127.0.0.1", 9990);

	spawn {
		net.Socket conn = net.tcp_connect("127.0.0.1", 9990);
		conn.write("hello");
		string reply = conn.read(64);
		conn.close();
	}

	net.Socket conn = server.accept();
	string data = conn.read(64);
	conn.write(data);
	conn.close();
	server.close();
	return 0;
}
