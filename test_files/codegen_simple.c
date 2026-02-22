// Simple codegen test: functions that return constants
// This exercises the full pipeline: parse -> LLVM IR -> native

int answer()
{
	return 42;
}

int add_one( int x )
{
	return x;
}

int main()
{
	int x = 10;
	return 0;
}
