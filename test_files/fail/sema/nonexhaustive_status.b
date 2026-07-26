// U5 diagnostic coverage: another non-exhaustive match (three-variant enum).
enum Status { active, idle, done }
fn check(Status s) -> int {
	match s {
		active { return 1; }
	}
	return 0;
}
fn main() -> int { return 0; }
