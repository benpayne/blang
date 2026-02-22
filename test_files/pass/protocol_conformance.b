protocol Greetable {
	fn greet(self) -> string;
}

struct Person {
	string name;
}

impl Greetable for Person {
	fn greet(self) -> string {
		return "hello";
	}
}
