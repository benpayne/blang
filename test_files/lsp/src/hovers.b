// LSP fixture: hover targets including a generic declared type.
fn scale(int value, int factor) -> int {
	return value * factor;
}

fn main() -> int {
	Array<int> nums = [1, 2, 3];
	int doubled = scale(nums[0], 2);
	return doubled;
}
