// E2E test: sys.args returns command-line arguments as Array<string>
// The first element is the program name.

import sys;

fn main() -> int {
	Array<string> args = sys.args;
	// args[0] is the program binary path — just verify we got at least 1 arg
	int count = args.length;
	if count < 1 {
		return 1;
	}
	return 0;
}
