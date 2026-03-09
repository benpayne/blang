// stdlib/sys.b — System module: command-line arguments and process control
//
// Usage: import sys;
//   sys.args        -> Array<string> (command-line arguments)
//   sys.exit(code)  -> exits the process

extern fn __blang_sys_get_args() -> Array<string>;
extern fn __blang_sys_exit(int code);

pub fn args() -> Array<string> {
	return __blang_sys_get_args();
}

pub fn exit(int code) {
	__blang_sys_exit(code);
}
