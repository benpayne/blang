// audit_04: incompatible initializer type. Today the initializer is silently dropped.
fn main() -> int {
	int x = "hello";
	return 0;
}
