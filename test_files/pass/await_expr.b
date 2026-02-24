// Await expression in async function

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
