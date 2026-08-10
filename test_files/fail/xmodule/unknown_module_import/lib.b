// A real dependency, present on the command line as lib.bmod — so the compile
// has an AUTHORITATIVE module set and an import of a module that is NOT available
// is a located error (rather than being leniently ignored as in a bare
// single-file parse).
pub fn hello() -> int {
	return 1;
}
