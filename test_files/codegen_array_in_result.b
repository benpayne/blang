// Regression: a refcounted heap payload (Array) carried through Result must not
// be freed by its origin scope before the enum is consumed. Before the fix, the
// local array returned via Result.ok(a) was released at function exit, so the
// unwrapped array was garbage (wrong length / use-after-free). Correct behavior
// sums to 60 and exits 0.

fn make() -> Result<Array<int>, string> {
	Array<int> a = [];
	a.push(10);
	a.push(20);
	a.push(30);
	return Result.ok(a);
}

fn sum() -> Result<int, string> {
	Array<int> a = make()?;
	int total = 0;
	for i in 0..a.length {
		total = total + a[i];
	}
	return Result.ok(total);
}

fn main() -> int {
	Result<int, string> r = sum();
	match r {
		ok(v) {
			if v == 60 {
				return 0;
			}
			return 1;
		}
		err(e) {
			return 2;
		}
	}
}
