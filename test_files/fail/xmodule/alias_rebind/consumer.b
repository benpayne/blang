// modules-v2-graph U8 (DC10) — import aliasing REBINDS the module to the local
// qualifier; it does not ALSO keep the original name bound (D8: no implicit
// re-export / dual binding). After `import lib as l;`, `l.greet(...)` is the only
// path — the original `lib.greet(...)` is a located error.
import lib as l;

fn main() -> int {
	string r = lib.greet("world");
	return 0;
}
