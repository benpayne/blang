// Test: print with format placeholders

fn main() -> int {
	int x = 42;
	float y = 3.14;
	string name = "world";
	bool flag = true;

	println("x={} y={}", x, y);
	println("hello {}", name);
	println("flag is {}", flag);
	println("{} + {} = {}", x, x, x);
	return 0;
}
