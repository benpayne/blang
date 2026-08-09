import lib;

fn main() -> int {
	Pair<int> p = Pair<int>(10, 32);
	// `first` is a private member of the imported generic struct — read it via
	// the accessor p.first(), never the field.
	return p.first;
}
