// E2E test: async/await basic functionality

async fn getData() -> int {
	return 42;
}

async fn process() -> int {
	int result = await getData();
	return result;
}

fn main() -> int {
	println("Async codegen test passed!");
	return 0;
}
