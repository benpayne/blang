extern fn puts(cstring s) -> int;

fn main() -> int {
	string hello = "hello";
	string world = "world";
	string combined = hello + " " + world;

	puts(combined);

	string msg = "test {hello}";
	puts(msg);

	bool eq = (hello == world);
	bool eq2 = (hello == "hello");

	return 0;
}
