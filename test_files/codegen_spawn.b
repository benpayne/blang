// End-to-end codegen test for spawn blocks (sequential stubs)

fn main() -> int {
	int x = 0;

	spawn {
		int y = 42;
	}

	spawn {
		int a = 1;
		spawn {
			int b = 2;
		}
	}

	return 0;
}
