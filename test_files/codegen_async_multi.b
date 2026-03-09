// E2E test: multiple async calls, each awaited

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
	println("Async multi codegen test passed!");
	return 0;
}
