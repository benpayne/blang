// Injected-leak fixture for `./test_codegen.sh --leak-check` teeth (U4).
// Deliberately leaks a heap allocation: malloc without free. Under
// AddressSanitizer/LeakSanitizer this is reported as a leak, so --leak-check
// must exit NON-ZERO on this file. NOT named codegen_*.b, so the default suite
// never picks it up; it is only run when passed explicitly.
extern fn malloc(long size) -> long;

fn main() -> int
{
	long leaked = malloc(64);   // intentionally never freed
	return 0;
}
