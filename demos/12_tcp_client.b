// Demo 12: TCP Client
//
// Connects to a TCP server, sends a message, and prints the response.
// Uses sys.args for host and port from the command line.
//
// Run: bcc demos/12_tcp_client.b -o tcp_client
// Usage: ./tcp_client <host> <port> <message>
// Example: ./tcp_client 127.0.0.1 8080 "Hello, server!"

import sys;
import net;

fn main() -> int {
	Array<string> args = sys.args;
	if args.length < 4 {
		println("Usage: tcp_client <host> <port> <message>");
		return 1;
	}

	string host = args[1];
	string portStr = args[2];
	int port = portStr.to_int();
	string message = args[3];

	println("Connecting to {}:{}...", host, port);
	net.Socket conn = net.tcp_connect(host, port);

	println("Sending: {}", message);
	conn.write(message);

	string reply = conn.read(4096);
	println("Received: {}", reply);

	conn.close();
	return 0;
}
