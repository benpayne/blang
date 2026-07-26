// Poison input for the parser-reachability teeth proof (U5).
// With the REAL parser this is a valid program that parses cleanly. In the code
// audit the parser is temporarily broken to abort() when it parses a function
// named `__fuzz_reaches_parser__`; feeding this poison to fuzz_parse then crashes,
// proving the input bytes flow lexer -> parser. Reverting the break -> clean.
fn __fuzz_reaches_parser__() -> int
{
	return 0;
}
