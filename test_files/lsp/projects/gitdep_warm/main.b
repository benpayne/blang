// modules-v2-lsp U3 fixture: the same git dependency as gitdep_unfetched, but
// the cache (warm_cache/) already holds its checkout, so it resolves from source
// with no git ever run. Expect a clean publish.
import greet;

fn main() -> int {
	int v = greet.hi();
	println("{}", v);
	return 0;
}
