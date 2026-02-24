// End-to-end codegen test for async/await (synchronous stubs)

async fn getData() -> int {
	return 42;
}

async fn process() -> int {
	int result = await getData();
	return result;
}

fn main() -> int {
	return 0;
}
