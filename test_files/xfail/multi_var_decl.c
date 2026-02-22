// Multiple variable declarations in one statement - causes parser hang
// e.g. "int x = 42, y;" loops infinitely

int main()
{
	int x = 42, y;
}
