// EXPECT-ERROR: takes 1 parameter\(s\) but protocol 'Greeter' requires 2
protocol Greeter {
	fn greet(self, string name) -> string;
}

struct Bot {
	int id;
}

impl Greeter for Bot {
	fn greet(self) -> string {
		return "hi";
	}
}

fn main() -> int {
	return 0;
}
