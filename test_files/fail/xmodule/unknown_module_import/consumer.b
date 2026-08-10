// modules-v2-graph U6b-2 (DC8): importing a module that no dependency or stdlib
// provides is a located error in an authoritative build (a dependency .bmod is on
// the command line, so the available module set is known).
import nonesuch;

fn main() -> int {
	return 0;
}
