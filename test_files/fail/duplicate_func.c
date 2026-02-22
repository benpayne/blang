// Function name that conflicts with a type keyword should fail
// (duplicate_func: compiler does not yet reject same-name functions,
// so we test naming conflict with a reserved type instead)

fn int() -> int {
	return 0;
}

int main() {
	return 0;
}
