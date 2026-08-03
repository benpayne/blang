// Example #6 — a TCP chat (broadcast) server.
//
// The first example to combine ALL of BLang's concurrency pieces in one
// program:
//   - the Selector event loop (runs on a spawned thread; lambda callbacks)
//   - spawn blocks (the selector thread; scripted clients in tests)
//   - a chan<int> crossing threads: the "/quit" handler on the selector
//     thread signals the main thread to shut down
//   - the net stdlib (ServerSocket/Socket, accept/read/write/close)
//
// Modes (selected by argv):
//   chat server <port>                start a broadcast server
//   chat client <port> <msg> <reads>  connect, send <msg> ("-" = send
//                                     nothing), then read <reads> broadcast
//                                     lines and print them
//   chat quit <port>                  connect and send "/quit"
//
// Server behavior: every line received from one client is relayed verbatim to
// every OTHER connected client. "/quit" shuts the server down.

import net;
import sys;

// Connected-client registry + stats. Mutated ONLY from selector callbacks,
// which all run on the selector thread — single-threaded access by design.
struct Room {
	Array<int> fds;
	int relayed;
}

impl Room {
	fn join(self, int fd) {
		self.fds.push(fd);
	}

	fn leave(self, int fd) {
		Array<int> keep = [];
		for f in self.fds {
			if f != fd {
				keep.push(f);
			}
		}
		self.fds = keep;
	}

	// Relay a line to every client except the sender.
	fn broadcast(self, int sender_fd, string line) {
		for f in self.fds {
			if f != sender_fd {
				net.Socket out = net.Socket { fd: f };
				out.write(line);
			}
		}
		self.relayed = self.relayed + 1;
	}
}

fn main() -> int {
	// sys.args() includes the program name at index 0.
	Array<string> args = sys.args();
	if args.length < 3 {
		println("usage: chat server <port> | chat client <port> <msg> <reads> | chat quit <port>");
		return 1;
	}
	string mode = args[1];
	int port = args[2].to_int();

	if mode == "server" {
		return run_server(port);
	}
	if mode == "client" {
		string msg = args[3];
		int reads = args[4].to_int();
		return run_client(port, msg, reads);
	}
	if mode == "quit" {
		net.Socket c = net.tcp_connect("127.0.0.1", port);
		c.write("/quit");
		c.close();
		return 0;
	}
	println("unknown mode: {}", mode);
	return 1;
}

fn run_server(int port) -> int {
	net.ServerSocket server = net.server_bind("127.0.0.1", port);
	net.Selector sel = net.selector_create();
	Room room = Room { fds: [], relayed: 0 };
	chan<int> done;

	sel.on_accept(server, fn(net.Socket conn) {
		room.join(conn.fd);
		println("client joined ({} connected)", room.fds.length);

		sel.on_data(conn, fn(net.Socket c) {
			string line = c.read(512);
			if line.length == 0 {
				// Peer closed: unregister and drop from the room.
				sel.remove(c);
				room.leave(c.fd);
				c.close();
				println("client left ({} connected)", room.fds.length);
			} else if line == "/quit" {
				done.send(1);
			} else {
				room.broadcast(c.fd, line);
				println("relayed: {}", line);
			}
		});
	});

	println("chat server on {}", port);

	// Block until a "/quit" arrives from the selector thread.
	match done.recv() {
		some(v) {
			println("shutdown requested");
		}
		none {
			println("channel closed");
		}
	}

	sel.shutdown();
	sel.join();
	server.close();
	println("server done, relayed {} lines", room.relayed);
	return 0;
}

fn run_client(int port, string msg, int reads) -> int {
	net.Socket c = net.tcp_connect("127.0.0.1", port);
	if msg != "-" {
		c.write(msg);
	}
	int i = 0;
	while i < reads {
		string line = c.read(512);
		if line.length == 0 {
			c.close();
			return 1;
		}
		println("{}", line);
		i = i + 1;
	}
	c.close();
	return 0;
}
