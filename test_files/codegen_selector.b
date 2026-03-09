// E2E test: Selector event loop — register accept + data callbacks,
// echo one connection then shut down.
// Callbacks receive Socket — no raw fds exposed to user code.

import net;

fn main() -> int {
	net.ServerSocket server = net.server_bind("127.0.0.1", 9991);
	net.Selector sel = net.selector_create();

	sel.on_accept(server, fn(net.Socket conn) {
		sel.on_data(conn, fn(net.Socket s) {
			string data = s.read(4096);
			s.write(data);
			s.close();
			sel.shutdown();
		});
	});

	spawn {
		net.Socket client = net.tcp_connect("127.0.0.1", 9991);
		client.write("selector test");
		string reply = client.read(64);
		client.close();
	}

	sel.join();
	server.close();
	return 0;
}
