// Test: method calls on function return values (chaining)

extern fn printf(cstring fmt, ...) -> int;

struct Info {
	int code;
	int flag;
}

impl Info {
	fn is_valid(self) -> bool {
		return self.code >= 0;
	}

	fn get_code(self) -> int {
		return self.code;
	}

	fn has_flag(self) -> bool {
		return self.flag != 0;
	}
}

fn make_info(int code, int flag) -> Info {
	return Info { code: code, flag: flag };
}

fn get_valid_info() -> Info {
	return Info { code: 42, flag: 1 };
}

fn get_invalid_info() -> Info {
	return Info { code: -1, flag: 0 };
}

fn main() -> int {
	// Test method call on function return value: fn().method()
	bool valid = get_valid_info().is_valid();
	if valid != true { printf("FAIL: get_valid_info().is_valid() should be true\n"); return 1; }

	bool invalid = get_invalid_info().is_valid();
	if invalid != false { printf("FAIL: get_invalid_info().is_valid() should be false\n"); return 1; }

	// Test method with return value on fn return value
	int code = get_valid_info().get_code();
	if code != 42 { printf("FAIL: get_valid_info().get_code() returned %d, expected 42\n", code); return 1; }

	// Test with parameterized function
	bool custom = make_info(100, 1).has_flag();
	if custom != true { printf("FAIL: make_info(100,1).has_flag() should be true\n"); return 1; }

	bool no_flag = make_info(100, 0).has_flag();
	if no_flag != false { printf("FAIL: make_info(100,0).has_flag() should be false\n"); return 1; }

	printf("All method chain tests passed!\n");
	return 0;
}
