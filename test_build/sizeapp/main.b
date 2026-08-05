import sizelib;

fn main() -> int {
	Box b = Box(11);
	println("size = {}", b.size());
	println("secret = {}", b.secret());
	return 0;
}
