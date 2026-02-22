// Struct with invalid field (no type) should fail

struct Point {
	x;
	int y;
}

int main() {
	return 0;
}
