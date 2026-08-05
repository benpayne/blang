// MAJOR-3: a generic ARGUMENT travels with the type. `Box<Secret>` exports
// Secret even though `Box` itself is exported, so the check must recurse into
// type parameters BEFORE it returns on the container's own visibility.
struct Secret {
	int hidden;
}

pub struct Box<T> {
	T item;
}

pub fn take(Box<Secret> b) -> int {
	return 0;
}

fn main() -> int {
	return 0;
}
