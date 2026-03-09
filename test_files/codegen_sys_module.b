// E2E test: Module namespacing — import sys; sys.args, sys.exit()
// Validates that sys.args returns Array<string> and sys.exit() works.

import sys;

fn main() -> int {
	Array<string> args = sys.args;
	int count = args.length;
	if count < 1 {
		sys.exit(1);
	}
	sys.exit(0);
	return 0;
}
