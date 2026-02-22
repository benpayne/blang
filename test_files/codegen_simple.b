// Simple codegen test: functions that return constants
// This exercises the full pipeline: parse -> LLVM IR -> native

fn answer() -> int
{
	return 42;
}

fn add_one( int x ) -> int
{
	return x;
}

fn main() -> int
{
	int x = 10;
	return 0;
}
