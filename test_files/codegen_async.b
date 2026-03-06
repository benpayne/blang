// E2E test: async/await basic functionality

extern fn printf(cstring fmt, ...) -> int;

async fn getData() -> int {
	return 42;
}

async fn process() -> int {
	int result = await getData();
	return result;
}

fn main() -> int {
	printf("Async codegen test passed!\n");
	return 0;
}
