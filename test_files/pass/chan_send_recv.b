// Channel declaration with send/recv method calls

fn main() {
	chan<int> ch;
	ch.send(42);
	int x = ch.recv();
	ch.close();
}
