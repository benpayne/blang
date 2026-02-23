// A generic constraint that references a non-existent protocol should be rejected

fn sort<T: NonExistentProtocol>(T x) -> T {
	return x;
}

fn main() -> int {
	return 0;
}
