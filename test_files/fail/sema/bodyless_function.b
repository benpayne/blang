// A bodyless free function is an INTERFACE form: it is what a .bmod carries so
// a consumer can resolve a library's exported functions. In ordinary source it
// used to codegen an empty function returning zero — a silent wrong answer.
// `extern fn` is the way to declare a symbol defined outside BLang.
fn helper(int x) -> int;

fn main() -> int {
	return helper(1);
}
