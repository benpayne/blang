// audit_07 (design.md "The 10 audit programs"): a plain (value-ownership) heap
// value captured across a spawn boundary. Today copied by raw pointer (a data
// race); U7 requires it to be declared shared or sync.
fn main() -> int {
	string msg = "hello";
	spawn {
		println("{}", msg);
	}
	return 0;
}
