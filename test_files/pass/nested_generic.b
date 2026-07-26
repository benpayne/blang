// Nested generic type arguments: the closing ">>" (and ">>>" one level deeper)
// must parse as close-brackets, not as the right-shift operator. Locked in by
// the lexer's SHIFT split in the type-argument parser (QType.cpp).

struct Pair<A, B> {
	A first;
	B second;
}

fn nested_array() -> int {
	Array<Array<int>> grid = [];
	Array<int> row = [];
	row.push(1);
	grid.push(row);
	return grid.length;
}

fn triple_nested() -> int {
	Array<Array<Array<int>>> cube = [];
	return cube.length;
}

fn pair_with_array(Pair<string, Array<int>> p) -> int {
	Array<int> items = p.second;
	return items.length;
}

fn shift_still_works(int x) -> int {
	// Expression context: >> must still lex as right-shift.
	return x >> 2;
}

fn main() -> int {
	return nested_array() + triple_nested() + shift_still_works(16);
}
