// Multiple test blocks in one file

fn add(int a, int b) -> int {
	return a;
}

fn sub(int a, int b) -> int {
	return a;
}

test "add works" {
	int x = add(1, 2);
}

test "sub works" {
	int y = sub(5, 3);
}
