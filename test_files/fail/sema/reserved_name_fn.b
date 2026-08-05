// The compiler reserves the "__" symbol family for names it synthesizes
// (__<Struct>_dtor, __<Struct>_new, __enum_<Name>_box_dtor). A source
// declaration mangling into that family could collide with one of them.
fn __Counter_new(int start) -> int {
	return start;
}

fn main() -> int {
	return 0;
}
