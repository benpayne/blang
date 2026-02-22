// Generic type arguments in variable declarations

struct Box<T> {
	T value;
}

fn main() {
	Box<int> intBox;
	Box<string> strBox;
}
