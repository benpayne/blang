pub fn base() -> int {
	return 40;
}

pub fn helper() -> int {
	return 1;
}

pub fn wrap<T>(T v) -> T {
	if base() > 0 {
		return v;
	}
	return v;
}

pub fn tail() -> int {
	return 3;
}
