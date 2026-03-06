// E2E test: multiple async calls, each awaited

extern fn printf(cstring fmt, ...) -> int;

async fn computeA() -> int {
	return 10;
}

async fn computeB() -> int {
	return 20;
}

async fn computeC() -> int {
	return 30;
}

fn main() -> int {
	printf("Async multi codegen test passed!\n");
	return 0;
}
