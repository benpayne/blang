// Method call expression

struct List {
	int size;
}

impl List {
	fn length(self) -> int {
		return self.size;
	}

	fn add(self, int item) {
		int x = 0;
	}
}

fn run_test(List list) {
	list.add(42);
	int len = list.length();
}

fn main() -> int {
	return 0;
}
