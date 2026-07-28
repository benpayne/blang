// EXPECT-ERROR: returns 'int' but protocol 'Greeter' requires 'string'
protocol Greeter {
	fn greet(self, string name) -> string;
}

struct Bot {
	int id;
}

impl Greeter for Bot {
	fn greet(self, string name) -> int {
		return 1;
	}
}

fn main() -> int {
	return 0;
}
